// d3d9_vkbridge.cpp - the Vulkan renderer of the native builds: the engine's Direct3D 9 interfaces over DXVK.
//
// The OpenGL ES shim in this directory implements D3D9 itself. This file implements the same interfaces by handing
// every call to DXVK's native D3D9 (libdxvk_d3d9.so), which translates to Vulkan. Direct3DCreate9
// (d3d9_direct3d.cpp) returns d3d9vk::Create() when the Vulkan renderer is chosen (bo1_renderer 1) and falls back
// to the shim when DXVK or a Vulkan driver is missing.
//
// It is a bridge and not a direct use of DXVK's objects because of pointer width: in the 32-bit-pointer builds
// (tools/p32) this file is rewritten like the engine, so the interfaces below take and return 4-byte pointers,
// while DXVK is an ordinary 64-bit library. Calls go through the C functions of src/native/vk/bo1vk.h, which have
// no pointer types. Each engine-visible object here wraps one DXVK object (a 64-bit handle).
//
// Locks: DXVK returns addresses anywhere in the 64-bit space (mapped Vulkan memory, its own heap). When such an
// address does not fit 32 bits the engine gets a buffer of its own heap (below 4 GB) instead, filled from DXVK's
// memory on Lock (unless the lock discards) and copied back on Unlock (unless read-only). Addresses that fit are
// passed through.
//
// Not implemented (the engine does not call them): state blocks, fixed-function lights/materials, palettes,
// patches, ProcessVertices, the Get* state readers except those below, private data, GetContainer, GetDC.
#if defined(BO1_NATIVE) && defined(BO1_DXVK)

#include <d3d9.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../native/vk/bo1vk.h"

namespace d3d9vk
{
static inline bo1vk_p P(const void *p) { return (bo1vk_p)(uintptr_t)p; }

static HRESULT NotImplemented(const char *method)
{
    static int warned;
    if (warned < 40)
    {
        ++warned;
        fprintf(stderr, "vulkan: IDirect3D*9::%s is not implemented by the bridge\n", method);
    }
    return D3DERR_INVALIDCALL;
}
#define VK_NOTIMPL(name) return NotImplemented(name)

// One lock's memory: DXVK's address itself, or a low stand-in copied from / to it.
struct Bounce
{
    void *shadow = nullptr;
    bo1vk_p native = 0;
    uint64_t bytes = 0;
    bool writeBack = false;

    void *Begin(bo1vk_p address, uint64_t size, bool copyIn, bool write)
    {
        End();
        native = address;
        bytes = size;
        writeBack = write;
        if (!(address >> 32) && !((address + size) >> 32))
            return (void *)(uintptr_t)address;
        shadow = malloc(size ? (size_t)size : 1);
        if (!shadow)
            return nullptr;
        if (copyIn)
            bo1vk_copy(P(shadow), address, size);
        return shadow;
    }

    void End()
    {
        if (!shadow)
            return;
        if (writeBack)
            bo1vk_copy(native, P(shadow), bytes);
        free(shadow);
        shadow = nullptr;
    }
};

static bool LockCopiesIn(DWORD flags) { return !(flags & D3DLOCK_DISCARD); }
static bool LockWritesBack(DWORD flags) { return !(flags & D3DLOCK_READONLY); }

template <class I> class Unknown : public I
{
public:
    bo1vk_h n = 0;   // the DXVK object; one reference is this wrapper's
    LONG refs = 1;

    virtual ~Unknown()
    {
        if (n)
            bo1vk_release(n);
    }
    HRESULT QueryInterface(REFIID, void **object) override
    {
        if (object)
            *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return (ULONG)__sync_add_and_fetch(&refs, 1); }
    ULONG Release() override
    {
        const LONG left = __sync_sub_and_fetch(&refs, 1);
        if (left == 0)
            delete this;
        return (ULONG)left;
    }
};

template <class I, D3DRESOURCETYPE Type> class Resource : public Unknown<I>
{
public:
    IDirect3DDevice9 *device = nullptr;   // not counted: the engine releases the device last

    HRESULT GetDevice(IDirect3DDevice9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        *out = device;
        if (device)
            device->AddRef();
        return D3D_OK;
    }
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_INVALIDCALL; }
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD SetPriority(DWORD) override { return 0; }
    DWORD GetPriority() override { return 0; }
    void PreLoad() override { bo1vk_res_preload(this->n); }
    D3DRESOURCETYPE GetType() override { return Type; }
};

template <class I, D3DRESOURCETYPE Type> class BaseTexture : public Resource<I, Type>
{
public:
    DWORD SetLOD(DWORD) override { return 0; }
    DWORD GetLOD() override { return 0; }
    DWORD GetLevelCount() override { return bo1vk_tex_level_count(this->n); }
    HRESULT SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
    void GenerateMipSubLevels() override { bo1vk_tex_generate_mips(this->n); }
};

class Surface : public Resource<IDirect3DSurface9, D3DRTYPE_SURFACE>
{
public:
    Bounce lock;

    HRESULT GetContainer(REFIID, void **) override { VK_NOTIMPL("Surface9::GetContainer"); }
    HRESULT GetDesc(D3DSURFACE_DESC *desc) override { return bo1vk_tex_desc(n, BO1VK_SURFACE, 0, P(desc)); }
    HRESULT LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override
    {
        if (!locked)
            return D3DERR_INVALIDCALL;
        int32_t pitch = 0;
        uint64_t bits = 0, bytes = 0;
        const HRESULT hr = bo1vk_tex_lock(n, BO1VK_SURFACE, 0, 0, P(rect), flags, P(&pitch), P(&bits), P(&bytes));
        if (FAILED(hr))
            return hr;
        locked->Pitch = pitch;
        locked->pBits = lock.Begin(bits, bytes, LockCopiesIn(flags), LockWritesBack(flags));
        return locked->pBits ? hr : E_OUTOFMEMORY;
    }
    HRESULT UnlockRect() override
    {
        lock.End();
        return bo1vk_tex_unlock(n, BO1VK_SURFACE, 0, 0);
    }
    HRESULT GetDC(struct HDC__ **) override { VK_NOTIMPL("Surface9::GetDC"); }
    HRESULT ReleaseDC(struct HDC__ *) override { VK_NOTIMPL("Surface9::ReleaseDC"); }
};

static HRESULT WrapSurface(HRESULT hr, bo1vk_h native, IDirect3DDevice9 *device, IDirect3DSurface9 **out)
{
    *out = nullptr;
    if (FAILED(hr) || !native)
        return FAILED(hr) ? hr : D3DERR_INVALIDCALL;
    Surface *surface = new Surface;
    surface->n = native;
    surface->device = device;
    *out = surface;
    return hr;
}

// the locks of a texture's levels (and faces) that are open
struct LevelLocks
{
    enum { MAX = 6 * 16 };
    Bounce lock[MAX];
    Bounce &At(unsigned face, unsigned level) { return lock[(face * 16 + level) % MAX]; }
};

class Texture : public BaseTexture<IDirect3DTexture9, D3DRTYPE_TEXTURE>
{
public:
    LevelLocks locks;

    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override { return bo1vk_tex_desc(n, BO1VK_TEX2D, level, P(desc)); }
    HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_tex_surface(n, BO1VK_TEX2D, 0, level, P(&native)), native, device, out);
    }
    HRESULT LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override
    {
        if (!locked)
            return D3DERR_INVALIDCALL;
        int32_t pitch = 0;
        uint64_t bits = 0, bytes = 0;
        const HRESULT hr = bo1vk_tex_lock(n, BO1VK_TEX2D, 0, level, P(rect), flags, P(&pitch), P(&bits), P(&bytes));
        if (FAILED(hr))
            return hr;
        locked->Pitch = pitch;
        locked->pBits = locks.At(0, level).Begin(bits, bytes, LockCopiesIn(flags), LockWritesBack(flags));
        return locked->pBits ? hr : E_OUTOFMEMORY;
    }
    HRESULT UnlockRect(UINT level) override
    {
        locks.At(0, level).End();
        return bo1vk_tex_unlock(n, BO1VK_TEX2D, 0, level);
    }
    HRESULT AddDirtyRect(const RECT *rect) override { return bo1vk_tex_dirty(n, BO1VK_TEX2D, 0, P(rect)); }
};

class CubeTexture : public BaseTexture<IDirect3DCubeTexture9, D3DRTYPE_CUBETEXTURE>
{
public:
    LevelLocks locks;

    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override { return bo1vk_tex_desc(n, BO1VK_CUBE, level, P(desc)); }
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_tex_surface(n, BO1VK_CUBE, (uint32_t)face, level, P(&native)), native, device, out);
    }
    HRESULT LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override
    {
        if (!locked)
            return D3DERR_INVALIDCALL;
        int32_t pitch = 0;
        uint64_t bits = 0, bytes = 0;
        const HRESULT hr = bo1vk_tex_lock(n, BO1VK_CUBE, (uint32_t)face, level, P(rect), flags, P(&pitch), P(&bits),
            P(&bytes));
        if (FAILED(hr))
            return hr;
        locked->Pitch = pitch;
        locked->pBits = locks.At((unsigned)face, level).Begin(bits, bytes, LockCopiesIn(flags), LockWritesBack(flags));
        return locked->pBits ? hr : E_OUTOFMEMORY;
    }
    HRESULT UnlockRect(D3DCUBEMAP_FACES face, UINT level) override
    {
        locks.At((unsigned)face, level).End();
        return bo1vk_tex_unlock(n, BO1VK_CUBE, (uint32_t)face, level);
    }
    HRESULT AddDirtyRect(D3DCUBEMAP_FACES face, const RECT *rect) override
    {
        return bo1vk_tex_dirty(n, BO1VK_CUBE, (uint32_t)face, P(rect));
    }
};

class VolumeTexture : public BaseTexture<IDirect3DVolumeTexture9, D3DRTYPE_VOLUMETEXTURE>
{
public:
    LevelLocks locks;

    HRESULT GetLevelDesc(UINT level, D3DVOLUME_DESC *desc) override { return bo1vk_tex_desc(n, BO1VK_VOLUME, level, P(desc)); }
    HRESULT GetVolumeLevel(UINT, IDirect3DVolume9 **) override { VK_NOTIMPL("VolumeTexture9::GetVolumeLevel"); }
    HRESULT LockBox(UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags) override
    {
        if (!locked)
            return D3DERR_INVALIDCALL;
        int32_t rowPitch = 0, slicePitch = 0;
        uint64_t bits = 0, bytes = 0;
        const HRESULT hr = bo1vk_vol_lock(n, level, P(box), flags, P(&rowPitch), P(&slicePitch), P(&bits), P(&bytes));
        if (FAILED(hr))
            return hr;
        locked->RowPitch = rowPitch;
        locked->SlicePitch = slicePitch;
        locked->pBits = locks.At(0, level).Begin(bits, bytes, LockCopiesIn(flags), LockWritesBack(flags));
        return locked->pBits ? hr : E_OUTOFMEMORY;
    }
    HRESULT UnlockBox(UINT level) override
    {
        locks.At(0, level).End();
        return bo1vk_vol_unlock(n, level);
    }
    HRESULT AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
};

// Vertex and index buffers. The engine's locks name the range they write (R_LockVertexBuffer(handle, offset, bytes,
// flags)) or take the whole buffer (0, 0).
template <class I, D3DRESOURCETYPE Type, int IsIndex> class Buffer : public Resource<I, Type>
{
public:
    UINT length = 0;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    DWORD fvf = 0;
    Bounce lock;

    HRESULT Lock(UINT offset, UINT size, void **data, DWORD flags) override
    {
        if (!data)
            return D3DERR_INVALIDCALL;
        uint64_t address = 0;
        const HRESULT hr = bo1vk_buf_lock(this->n, IsIndex, offset, size, flags, P(&address));
        if (FAILED(hr))
            return hr;
        const UINT bytes = size ? size : (offset < length ? length - offset : 0);
        // a range that is appended to (NOOVERWRITE) is written whole by the engine; nothing to read first
        const bool copyIn = LockCopiesIn(flags) && !((flags & D3DLOCK_NOOVERWRITE) && size);
        *data = lock.Begin(address, bytes, copyIn, LockWritesBack(flags));
        return *data ? hr : E_OUTOFMEMORY;
    }
    HRESULT Unlock() override
    {
        lock.End();
        return bo1vk_buf_unlock(this->n, IsIndex);
    }
};

class VertexBuffer : public Buffer<IDirect3DVertexBuffer9, D3DRTYPE_VERTEXBUFFER, 0>
{
public:
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *desc) override
    {
        if (!desc)
            return D3DERR_INVALIDCALL;
        desc->Format = D3DFMT_VERTEXDATA;
        desc->Type = D3DRTYPE_VERTEXBUFFER;
        desc->Usage = usage;
        desc->Pool = pool;
        desc->Size = length;
        desc->FVF = fvf;
        return D3D_OK;
    }
};

class IndexBuffer : public Buffer<IDirect3DIndexBuffer9, D3DRTYPE_INDEXBUFFER, 1>
{
public:
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *desc) override
    {
        if (!desc)
            return D3DERR_INVALIDCALL;
        desc->Format = format;
        desc->Type = D3DRTYPE_INDEXBUFFER;
        desc->Usage = usage;
        desc->Pool = pool;
        desc->Size = length;
        return D3D_OK;
    }
};

template <class I> class DeviceChild : public Unknown<I>
{
public:
    IDirect3DDevice9 *device = nullptr;
    HRESULT GetDevice(IDirect3DDevice9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        *out = device;
        if (device)
            device->AddRef();
        return D3D_OK;
    }
};

class VertexDeclaration : public DeviceChild<IDirect3DVertexDeclaration9>
{
public:
    HRESULT GetDeclaration(D3DVERTEXELEMENT9 *, UINT *) override { VK_NOTIMPL("VertexDeclaration9::GetDeclaration"); }
};

class VertexShader : public DeviceChild<IDirect3DVertexShader9>
{
public:
    HRESULT GetFunction(void *, UINT *) override { VK_NOTIMPL("VertexShader9::GetFunction"); }
};

class PixelShader : public DeviceChild<IDirect3DPixelShader9>
{
public:
    HRESULT GetFunction(void *, UINT *) override { VK_NOTIMPL("PixelShader9::GetFunction"); }
};

class Query : public DeviceChild<IDirect3DQuery9>
{
public:
    D3DQUERYTYPE type = D3DQUERYTYPE_OCCLUSION;
    D3DQUERYTYPE GetType() override { return type; }
    DWORD GetDataSize() override { return bo1vk_query_data_size(n); }
    HRESULT Issue(DWORD flags) override { return bo1vk_query_issue(n, flags); }
    HRESULT GetData(void *data, DWORD size, DWORD flags) override { return bo1vk_query_data(n, P(data), size, flags); }
};

static void ToParams(const D3DPRESENT_PARAMETERS &pp, bo1vk_present_params *out)
{
    out->width = pp.BackBufferWidth;
    out->height = pp.BackBufferHeight;
    out->format = (uint32_t)pp.BackBufferFormat;
    out->count = pp.BackBufferCount;
    out->multisample = (uint32_t)pp.MultiSampleType;
    out->quality = pp.MultiSampleQuality;
    out->swapEffect = (uint32_t)pp.SwapEffect;
    out->windowed = (uint32_t)pp.Windowed;
    out->autoDepthStencil = (uint32_t)pp.EnableAutoDepthStencil;
    out->depthStencilFormat = (uint32_t)pp.AutoDepthStencilFormat;
    out->flags = pp.Flags;
    out->refreshRate = pp.FullScreen_RefreshRateInHz;
    out->interval = pp.PresentationInterval;
}

static void FromParams(const bo1vk_present_params &in, D3DPRESENT_PARAMETERS *pp)
{
    pp->BackBufferWidth = in.width;
    pp->BackBufferHeight = in.height;
    pp->BackBufferFormat = (D3DFORMAT)in.format;
    pp->BackBufferCount = in.count;
}

class SwapChain : public DeviceChild<IDirect3DSwapChain9>
{
public:
    D3DPRESENT_PARAMETERS params;

    HRESULT Present(const RECT *, const RECT *, HWND, const struct _RGNDATA *, DWORD flags) override
    {
        return bo1vk_swap_present(n, flags);
    }
    HRESULT GetFrontBufferData(IDirect3DSurface9 *dst) override
    {
        return dst ? bo1vk_swap_front_buffer_data(n, static_cast<Surface *>(dst)->n) : D3DERR_INVALIDCALL;
    }
    HRESULT GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_swap_back_buffer(n, index, P(&native)), native, device, out);
    }
    HRESULT GetRasterStatus(D3DRASTER_STATUS *) override { VK_NOTIMPL("SwapChain9::GetRasterStatus"); }
    HRESULT GetDisplayMode(D3DDISPLAYMODE *) override { VK_NOTIMPL("SwapChain9::GetDisplayMode"); }
    HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS *out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        *out = params;
        return D3D_OK;
    }
};

template <class T> static bo1vk_h Native(T *object) { return object ? object->n : 0; }
static bo1vk_h NativeSurface(IDirect3DSurface9 *surface) { return Native(static_cast<Surface *>(surface)); }

// Any of the three texture classes: the DXVK object is at the same place in each (Unknown<I>::n follows the
// interface's vtable pointer), read through the 2D class.
static bo1vk_h NativeTexture(IDirect3DBaseTexture9 *texture)
{
    return texture ? reinterpret_cast<Texture *>(texture)->n : 0;
}

class Device : public Unknown<IDirect3DDevice9>
{
public:
    IDirect3D9 *d3d = nullptr;
    D3DPRESENT_PARAMETERS params;
    D3DDEVICE_CREATION_PARAMETERS creation;
    struct Stream
    {
        IDirect3DVertexBuffer9 *buffer;
        UINT offset, stride;
    } streams[16];

    Device() { memset(streams, 0, sizeof(streams)); }
    ~Device() override
    {
        if (d3d)
            d3d->Release();
    }

    template <class T> T *Wrap(HRESULT hr, bo1vk_h native)
    {
        if (FAILED(hr) || !native)
            return nullptr;
        T *object = new T;
        object->n = native;
        object->device = this;
        return object;
    }

    HRESULT TestCooperativeLevel() override { return bo1vk_dev_test_cooperative(n); }
    UINT GetAvailableTextureMem() override { return bo1vk_dev_texture_mem(n); }
    HRESULT EvictManagedResources() override { return bo1vk_dev_evict(n); }
    HRESULT GetDirect3D(IDirect3D9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        *out = d3d;
        d3d->AddRef();
        return D3D_OK;
    }
    HRESULT GetDeviceCaps(D3DCAPS9 *caps) override { return bo1vk_dev_caps(n, P(caps)); }
    HRESULT GetDisplayMode(UINT swapChain, D3DDISPLAYMODE *mode) override { return bo1vk_dev_display_mode(n, swapChain, P(mode)); }
    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        *out = creation;
        return D3D_OK;
    }
    HRESULT SetCursorProperties(UINT, UINT, IDirect3DSurface9 *) override { return D3D_OK; }
    void SetCursorPosition(int, int, DWORD) override {}
    BOOL ShowCursor(BOOL) override { return FALSE; }
    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **) override
    {
        VK_NOTIMPL("Device9::CreateAdditionalSwapChain");
    }
    HRESULT GetSwapChain(UINT index, IDirect3DSwapChain9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_swapchain(n, index, P(&native));
        SwapChain *chain = Wrap<SwapChain>(hr, native);
        if (chain)
            chain->params = params;
        *out = chain;
        return hr;
    }
    UINT GetNumberOfSwapChains() override { return 1; }
    HRESULT Reset(D3DPRESENT_PARAMETERS *pp) override
    {
        if (!pp)
            return D3DERR_INVALIDCALL;
        bo1vk_present_params flat;
        ToParams(*pp, &flat);
        const HRESULT hr = bo1vk_dev_reset(n, P(&flat));
        if (SUCCEEDED(hr))
        {
            FromParams(flat, pp);
            params = *pp;
            memset(streams, 0, sizeof(streams));
        }
        return hr;
    }
    HRESULT Present(const RECT *, const RECT *, HWND, const struct _RGNDATA *) override { return bo1vk_dev_present(n); }
    HRESULT GetBackBuffer(UINT swapChain, UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_dev_back_buffer(n, swapChain, index, P(&native)), native, this, out);
    }
    HRESULT GetRasterStatus(UINT swapChain, D3DRASTER_STATUS *status) override { return bo1vk_dev_raster_status(n, swapChain, P(status)); }
    HRESULT SetDialogBoxMode(BOOL) override { return D3D_OK; }
    void SetGammaRamp(UINT swapChain, DWORD flags, const D3DGAMMARAMP *ramp) override { bo1vk_dev_set_gamma(n, swapChain, flags, P(ramp)); }
    void GetGammaRamp(UINT, D3DGAMMARAMP *) override {}
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DTexture9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_texture(n, width, height, levels, usage, (uint32_t)format,
            (uint32_t)pool, P(&native));
        Texture *texture = Wrap<Texture>(hr, native);
        *out = texture;
        return hr;
    }
    HRESULT CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DVolumeTexture9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_volume(n, width, height, depth, levels, usage, (uint32_t)format,
            (uint32_t)pool, P(&native));
        VolumeTexture *texture = Wrap<VolumeTexture>(hr, native);
        *out = texture;
        return hr;
    }
    HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DCubeTexture9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_cube(n, edge, levels, usage, (uint32_t)format, (uint32_t)pool,
            P(&native));
        CubeTexture *texture = Wrap<CubeTexture>(hr, native);
        *out = texture;
        return hr;
    }
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **out,
        HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_vb(n, length, usage, fvf, (uint32_t)pool, P(&native));
        VertexBuffer *buffer = Wrap<VertexBuffer>(hr, native);
        if (buffer)
        {
            buffer->length = length;
            buffer->usage = usage;
            buffer->pool = pool;
            buffer->fvf = fvf;
        }
        *out = buffer;
        return hr;
    }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **out,
        HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_ib(n, length, usage, (uint32_t)format, (uint32_t)pool, P(&native));
        IndexBuffer *buffer = Wrap<IndexBuffer>(hr, native);
        if (buffer)
        {
            buffer->length = length;
            buffer->usage = usage;
            buffer->pool = pool;
            buffer->format = format;
        }
        *out = buffer;
        return hr;
    }
    HRESULT CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
        DWORD quality, BOOL lockable, IDirect3DSurface9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_dev_create_rt(n, width, height, (uint32_t)format, (uint32_t)multisample, quality,
            lockable, P(&native)), native, this, out);
    }
    HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
        DWORD quality, BOOL discard, IDirect3DSurface9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_dev_create_ds(n, width, height, (uint32_t)format, (uint32_t)multisample, quality,
            discard, P(&native)), native, this, out);
    }
    HRESULT UpdateSurface(IDirect3DSurface9 *src, const RECT *srcRect, IDirect3DSurface9 *dst, const POINT *dstPoint) override
    {
        return bo1vk_dev_update_surface(n, NativeSurface(src), P(srcRect), NativeSurface(dst), P(dstPoint));
    }
    HRESULT UpdateTexture(IDirect3DBaseTexture9 *src, IDirect3DBaseTexture9 *dst) override
    {
        return bo1vk_dev_update_texture(n, NativeTexture(src), NativeTexture(dst));
    }
    HRESULT GetRenderTargetData(IDirect3DSurface9 *renderTarget, IDirect3DSurface9 *dst) override
    {
        return bo1vk_dev_rt_data(n, NativeSurface(renderTarget), NativeSurface(dst));
    }
    HRESULT GetFrontBufferData(UINT swapChain, IDirect3DSurface9 *dst) override
    {
        return bo1vk_dev_front_buffer_data(n, swapChain, NativeSurface(dst));
    }
    HRESULT StretchRect(IDirect3DSurface9 *src, const RECT *srcRect, IDirect3DSurface9 *dst, const RECT *dstRect,
        D3DTEXTUREFILTERTYPE filter) override
    {
        return bo1vk_dev_stretch_rect(n, NativeSurface(src), P(srcRect), NativeSurface(dst), P(dstRect), (uint32_t)filter);
    }
    HRESULT ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override
    {
        return bo1vk_dev_color_fill(n, NativeSurface(surface), P(rect), color);
    }
    HRESULT CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
        IDirect3DSurface9 **out, HANDLE *) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        return WrapSurface(bo1vk_dev_create_plain(n, width, height, (uint32_t)format, (uint32_t)pool, P(&native)),
            native, this, out);
    }
    HRESULT SetRenderTarget(DWORD index, IDirect3DSurface9 *surface) override { return bo1vk_dev_set_rt(n, index, NativeSurface(surface)); }
    HRESULT GetRenderTarget(DWORD, IDirect3DSurface9 **) override { VK_NOTIMPL("Device9::GetRenderTarget"); }
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *surface) override { return bo1vk_dev_set_ds(n, NativeSurface(surface)); }
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **) override { VK_NOTIMPL("Device9::GetDepthStencilSurface"); }
    HRESULT BeginScene() override { return bo1vk_dev_begin_scene(n); }
    HRESULT EndScene() override { return bo1vk_dev_end_scene(n); }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
    {
        return bo1vk_dev_clear(n, count, P(rects), flags, color, z, stencil);
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override { return bo1vk_dev_set_transform(n, (uint32_t)state, P(matrix)); }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE, D3DMATRIX *) override { VK_NOTIMPL("Device9::GetTransform"); }
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { VK_NOTIMPL("Device9::MultiplyTransform"); }
    HRESULT SetViewport(const D3DVIEWPORT9 *viewport) override { return bo1vk_dev_set_viewport(n, P(viewport)); }
    HRESULT GetViewport(D3DVIEWPORT9 *viewport) override { return bo1vk_dev_get_viewport(n, P(viewport)); }
    HRESULT SetMaterial(const D3DMATERIAL9 *) override { VK_NOTIMPL("Device9::SetMaterial"); }
    HRESULT GetMaterial(D3DMATERIAL9 *) override { VK_NOTIMPL("Device9::GetMaterial"); }
    HRESULT SetLight(DWORD, const D3DLIGHT9 *) override { VK_NOTIMPL("Device9::SetLight"); }
    HRESULT GetLight(DWORD, D3DLIGHT9 *) override { VK_NOTIMPL("Device9::GetLight"); }
    HRESULT LightEnable(DWORD, BOOL) override { VK_NOTIMPL("Device9::LightEnable"); }
    HRESULT GetLightEnable(DWORD, BOOL *) override { VK_NOTIMPL("Device9::GetLightEnable"); }
    HRESULT SetClipPlane(DWORD index, const float *plane) override { return bo1vk_dev_set_clip_plane(n, index, P(plane)); }
    HRESULT GetClipPlane(DWORD, float *) override { VK_NOTIMPL("Device9::GetClipPlane"); }
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override { return bo1vk_dev_set_render_state(n, (uint32_t)state, value); }
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) override { return bo1vk_dev_get_render_state(n, (uint32_t)state, P(value)); }
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **) override { VK_NOTIMPL("Device9::CreateStateBlock"); }
    HRESULT BeginStateBlock() override { VK_NOTIMPL("Device9::BeginStateBlock"); }
    HRESULT EndStateBlock(IDirect3DStateBlock9 **) override { VK_NOTIMPL("Device9::EndStateBlock"); }
    HRESULT SetClipStatus(const D3DCLIPSTATUS9 *) override { return D3D_OK; }
    HRESULT GetClipStatus(D3DCLIPSTATUS9 *) override { VK_NOTIMPL("Device9::GetClipStatus"); }
    HRESULT GetTexture(DWORD, IDirect3DBaseTexture9 **) override { VK_NOTIMPL("Device9::GetTexture"); }
    HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) override { return bo1vk_dev_set_texture(n, stage, NativeTexture(texture)); }
    HRESULT GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD *) override { VK_NOTIMPL("Device9::GetTextureStageState"); }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override
    {
        return bo1vk_dev_set_stage_state(n, stage, (uint32_t)type, value);
    }
    HRESULT GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD *) override { VK_NOTIMPL("Device9::GetSamplerState"); }
    HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override
    {
        return bo1vk_dev_set_sampler_state(n, sampler, (uint32_t)type, value);
    }
    HRESULT ValidateDevice(DWORD *passes) override
    {
        if (passes)
            *passes = 1;
        return D3D_OK;
    }
    HRESULT SetPaletteEntries(UINT, const struct tagPALETTEENTRY *) override { VK_NOTIMPL("Device9::SetPaletteEntries"); }
    HRESULT GetPaletteEntries(UINT, struct tagPALETTEENTRY *) override { VK_NOTIMPL("Device9::GetPaletteEntries"); }
    HRESULT SetCurrentTexturePalette(UINT) override { VK_NOTIMPL("Device9::SetCurrentTexturePalette"); }
    HRESULT GetCurrentTexturePalette(UINT *) override { VK_NOTIMPL("Device9::GetCurrentTexturePalette"); }
    HRESULT SetScissorRect(const RECT *rect) override { return bo1vk_dev_set_scissor(n, P(rect)); }
    HRESULT GetScissorRect(RECT *) override { VK_NOTIMPL("Device9::GetScissorRect"); }
    HRESULT SetSoftwareVertexProcessing(BOOL) override { return D3D_OK; }
    BOOL GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT SetNPatchMode(float) override { return D3D_OK; }
    float GetNPatchMode() override { return 0.0f; }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count) override { return bo1vk_dev_draw(n, (uint32_t)type, start, count); }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex, UINT vertices, UINT startIndex,
        UINT count) override
    {
        return bo1vk_dev_draw_indexed(n, (uint32_t)type, baseVertex, minIndex, vertices, startIndex, count);
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void *data, UINT stride) override
    {
        return bo1vk_dev_draw_up(n, (uint32_t)type, count, P(data), stride);
    }
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT vertices, UINT count, const void *indices,
        D3DFORMAT indexFormat, const void *data, UINT stride) override
    {
        return bo1vk_dev_draw_indexed_up(n, (uint32_t)type, minIndex, vertices, count, P(indices), (uint32_t)indexFormat,
            P(data), stride);
    }
    HRESULT ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *, DWORD) override
    {
        VK_NOTIMPL("Device9::ProcessVertices");
    }
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_decl(n, P(elements), P(&native));
        VertexDeclaration *decl = Wrap<VertexDeclaration>(hr, native);
        *out = decl;
        return hr;
    }
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl) override
    {
        return bo1vk_dev_set_decl(n, Native(static_cast<VertexDeclaration *>(decl)));
    }
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **) override { VK_NOTIMPL("Device9::GetVertexDeclaration"); }
    HRESULT SetFVF(DWORD fvf) override { return bo1vk_dev_set_fvf(n, fvf); }
    HRESULT GetFVF(DWORD *) override { VK_NOTIMPL("Device9::GetFVF"); }
    HRESULT CreateVertexShader(const DWORD *function, IDirect3DVertexShader9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_vs(n, P(function), P(&native));
        VertexShader *shader = Wrap<VertexShader>(hr, native);
        *out = shader;
        return hr;
    }
    HRESULT SetVertexShader(IDirect3DVertexShader9 *shader) override
    {
        return bo1vk_dev_set_vs(n, Native(static_cast<VertexShader *>(shader)));
    }
    HRESULT GetVertexShader(IDirect3DVertexShader9 **) override { VK_NOTIMPL("Device9::GetVertexShader"); }
    HRESULT SetVertexShaderConstantF(UINT start, const float *data, UINT count) override { return bo1vk_dev_set_vs_const(n, 'F', start, P(data), count); }
    HRESULT GetVertexShaderConstantF(UINT, float *, UINT) override { VK_NOTIMPL("Device9::GetVertexShaderConstantF"); }
    HRESULT SetVertexShaderConstantI(UINT start, const int *data, UINT count) override { return bo1vk_dev_set_vs_const(n, 'I', start, P(data), count); }
    HRESULT GetVertexShaderConstantI(UINT, int *, UINT) override { VK_NOTIMPL("Device9::GetVertexShaderConstantI"); }
    HRESULT SetVertexShaderConstantB(UINT start, const BOOL *data, UINT count) override { return bo1vk_dev_set_vs_const(n, 'B', start, P(data), count); }
    HRESULT GetVertexShaderConstantB(UINT, BOOL *, UINT) override { VK_NOTIMPL("Device9::GetVertexShaderConstantB"); }
    HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) override
    {
        if (stream < 16)
        {
            streams[stream].buffer = buffer;
            streams[stream].offset = offset;
            streams[stream].stride = stride;
        }
        return bo1vk_dev_set_stream(n, stream, Native(static_cast<VertexBuffer *>(buffer)), offset, stride);
    }
    HRESULT GetStreamSource(UINT stream, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) override
    {
        if (stream >= 16 || !buffer)
            return D3DERR_INVALIDCALL;
        *buffer = streams[stream].buffer;
        if (*buffer)
            (*buffer)->AddRef();
        if (offset)
            *offset = streams[stream].offset;
        if (stride)
            *stride = streams[stream].stride;
        return D3D_OK;
    }
    HRESULT SetStreamSourceFreq(UINT stream, UINT setting) override { return bo1vk_dev_set_stream_freq(n, stream, setting); }
    HRESULT GetStreamSourceFreq(UINT, UINT *) override { VK_NOTIMPL("Device9::GetStreamSourceFreq"); }
    HRESULT SetIndices(IDirect3DIndexBuffer9 *buffer) override
    {
        return bo1vk_dev_set_indices(n, Native(static_cast<IndexBuffer *>(buffer)));
    }
    HRESULT GetIndices(IDirect3DIndexBuffer9 **) override { VK_NOTIMPL("Device9::GetIndices"); }
    HRESULT CreatePixelShader(const DWORD *function, IDirect3DPixelShader9 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_ps(n, P(function), P(&native));
        PixelShader *shader = Wrap<PixelShader>(hr, native);
        *out = shader;
        return hr;
    }
    HRESULT SetPixelShader(IDirect3DPixelShader9 *shader) override
    {
        return bo1vk_dev_set_ps(n, Native(static_cast<PixelShader *>(shader)));
    }
    HRESULT GetPixelShader(IDirect3DPixelShader9 **) override { VK_NOTIMPL("Device9::GetPixelShader"); }
    HRESULT SetPixelShaderConstantF(UINT start, const float *data, UINT count) override { return bo1vk_dev_set_ps_const(n, 'F', start, P(data), count); }
    HRESULT GetPixelShaderConstantF(UINT, float *, UINT) override { VK_NOTIMPL("Device9::GetPixelShaderConstantF"); }
    HRESULT SetPixelShaderConstantI(UINT start, const int *data, UINT count) override { return bo1vk_dev_set_ps_const(n, 'I', start, P(data), count); }
    HRESULT GetPixelShaderConstantI(UINT, int *, UINT) override { VK_NOTIMPL("Device9::GetPixelShaderConstantI"); }
    HRESULT SetPixelShaderConstantB(UINT start, const BOOL *data, UINT count) override { return bo1vk_dev_set_ps_const(n, 'B', start, P(data), count); }
    HRESULT GetPixelShaderConstantB(UINT, BOOL *, UINT) override { VK_NOTIMPL("Device9::GetPixelShaderConstantB"); }
    HRESULT DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) override { VK_NOTIMPL("Device9::DrawRectPatch"); }
    HRESULT DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) override { VK_NOTIMPL("Device9::DrawTriPatch"); }
    HRESULT DeletePatch(UINT) override { VK_NOTIMPL("Device9::DeletePatch"); }
    HRESULT CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **out) override
    {
        if (!out)
            return bo1vk_dev_create_query(n, (uint32_t)type, 0);
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_dev_create_query(n, (uint32_t)type, P(&native));
        Query *query = Wrap<Query>(hr, native);
        if (query)
            query->type = type;
        *out = query;
        return hr;
    }
};

IDirect3D9 *CreateModeSource();   // d3d9_direct3d.cpp

// The display modes are the OpenGL ES shim's list (the display size the platform layer reported,
// d3d9shim_set_display_size, and the standard sizes below it), not DXVK's: DXVK asks SDL, whose Android display
// mode is 16 bits per pixel, which leaves no 32-bit mode for the engine ("No valid resolutions of 800 x 600 or
// above found"). The window is the native build's own in any case.
class Direct3D : public Unknown<IDirect3D9>
{
public:
    IDirect3D9 *modes = nullptr;

    ~Direct3D() override
    {
        if (modes)
            modes->Release();
    }
    HRESULT RegisterSoftwareDevice(void *) override { VK_NOTIMPL("9::RegisterSoftwareDevice"); }
    UINT GetAdapterCount() override { return bo1vk_d3d_adapter_count(n); }
    HRESULT GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) override
    {
        return bo1vk_d3d_adapter_identifier(n, adapter, flags, P(identifier));
    }
    UINT GetAdapterModeCount(UINT adapter, D3DFORMAT format) override { return modes->GetAdapterModeCount(adapter, format); }
    HRESULT EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *out) override
    {
        return modes->EnumAdapterModes(adapter, format, mode, out);
    }
    HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *out) override { return modes->GetAdapterDisplayMode(adapter, out); }
    HRESULT CheckDeviceType(UINT adapter, D3DDEVTYPE devType, D3DFORMAT adapterFormat, D3DFORMAT backBufferFormat,
        BOOL windowed) override
    {
        return bo1vk_d3d_check_type(n, adapter, (uint32_t)devType, (uint32_t)adapterFormat, (uint32_t)backBufferFormat, windowed);
    }
    HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE devType, D3DFORMAT adapterFormat, DWORD usage,
        D3DRESOURCETYPE resourceType, D3DFORMAT format) override
    {
        return bo1vk_d3d_check_format(n, adapter, (uint32_t)devType, (uint32_t)adapterFormat, usage, (uint32_t)resourceType,
            (uint32_t)format);
    }
    HRESULT CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE devType, D3DFORMAT format, BOOL windowed,
        D3DMULTISAMPLE_TYPE type, DWORD *qualityLevels) override
    {
        return bo1vk_d3d_check_multisample(n, adapter, (uint32_t)devType, (uint32_t)format, windowed, (uint32_t)type,
            P(qualityLevels));
    }
    HRESULT CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE devType, D3DFORMAT adapterFormat, D3DFORMAT renderTargetFormat,
        D3DFORMAT depthFormat) override
    {
        return bo1vk_d3d_check_depth(n, adapter, (uint32_t)devType, (uint32_t)adapterFormat, (uint32_t)renderTargetFormat,
            (uint32_t)depthFormat);
    }
    HRESULT CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE devType, D3DFORMAT source, D3DFORMAT target) override
    {
        return bo1vk_d3d_check_conversion(n, adapter, (uint32_t)devType, (uint32_t)source, (uint32_t)target);
    }
    HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE devType, D3DCAPS9 *caps) override
    {
        return bo1vk_d3d_caps(n, adapter, (uint32_t)devType, P(caps));
    }
    HMONITOR GetAdapterMonitor(UINT) override { return (HMONITOR)1; }
    HRESULT CreateDevice(UINT adapter, D3DDEVTYPE devType, HWND focusWindow, DWORD behavior, D3DPRESENT_PARAMETERS *pp,
        IDirect3DDevice9 **out) override
    {
        if (!pp || !out)
            return D3DERR_INVALIDCALL;
        *out = nullptr;
        bo1vk_present_params flat;
        ToParams(*pp, &flat);
        bo1vk_h native = 0;
        const HRESULT hr = bo1vk_d3d_create_device(n, adapter, (uint32_t)devType, behavior, P(&flat), P(&native));
        if (FAILED(hr) || !native)
            return FAILED(hr) ? hr : D3DERR_NOTAVAILABLE;
        FromParams(flat, pp);
        Device *device = new Device;
        device->n = native;
        device->d3d = this;
        AddRef();
        device->params = *pp;
        device->creation.AdapterOrdinal = adapter;
        device->creation.DeviceType = devType;
        device->creation.hFocusWindow = focusWindow;
        device->creation.BehaviorFlags = behavior;
        *out = device;
        return hr;
    }
};

IDirect3D9 *Create()
{
    const bo1vk_h native = bo1vk_create();
    if (!native)
        return nullptr;
    Direct3D *d3d = new Direct3D;
    d3d->n = native;
    d3d->modes = CreateModeSource();
    return d3d;
}
} // namespace d3d9vk

#endif // BO1_NATIVE && BO1_DXVK
