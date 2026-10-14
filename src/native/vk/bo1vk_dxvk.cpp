// bo1vk_dxvk.cpp - the DXVK side of the Vulkan renderer's seam (bo1vk.h): each function is one Direct3D 9 call on
// DXVK's native D3D9, compiled against DXVK's own headers (include/native/directx, include/native/windows) with
// ordinary 64-bit pointers.
//
// DXVK is loaded at run time (dlopen): the program has its own Direct3DCreate9 (the OpenGL ES shim), and a build
// without the library still starts. Library: BO1_DXVK_LIB=<path>, else libdxvk_d3d9.so by the loader's search
// (next to the program by rpath; in the APK's library directory on Android).
//
// Without BO1_DXVK (no DXVK headers at build time) only bo1vk_available exists here, returning 0.
//
// This file is not rewritten (tools/p32).
#include "bo1vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BO1_DXVK

extern "C" int bo1vk_available(void)
{
    fprintf(stderr, "vulkan: this build has no DXVK side (configure with -DBO1_NATIVE_DXVK=<dxvk source>)\n");
    return 0;
}
extern "C" bo1vk_h bo1vk_create(void) { return 0; }

#else

#include <d3d9.h>
#include <dlfcn.h>
#include <unistd.h>

// native_lowmem.c: the graphics libraries allocate from a heap of their own (Linux; nothing on Android)
extern "C" void bo1_lowmem_scan_host_modules(void);

namespace
{
typedef IDirect3D9 *(WINAPI *CreateFn)(UINT);
CreateFn s_create;
bool s_tried;

template <class T> T *As(bo1vk_h h) { return (T *)(uintptr_t)h; }
template <class T> T *At(bo1vk_p p) { return (T *)(uintptr_t)p; }
#define DEV As<IDirect3DDevice9>(dev)
#define D3D As<IDirect3D9>(d3d)

template <class T> int32_t Out(HRESULT hr, T *object, bo1vk_p out)
{
    *At<bo1vk_h>(out) = SUCCEEDED(hr) ? (bo1vk_h)(uintptr_t)object : 0;
    return (int32_t)hr;
}

void ToD3D(const bo1vk_present_params &in, void *window, D3DPRESENT_PARAMETERS *pp)
{
    memset(pp, 0, sizeof(*pp));
    pp->BackBufferWidth = in.width;
    pp->BackBufferHeight = in.height;
    pp->BackBufferFormat = (D3DFORMAT)in.format;
    pp->BackBufferCount = in.count;
    pp->MultiSampleType = (D3DMULTISAMPLE_TYPE)in.multisample;
    pp->MultiSampleQuality = in.quality;
    pp->SwapEffect = (D3DSWAPEFFECT)in.swapEffect;
    pp->hDeviceWindow = (HWND)window;
    // a window of ours either way: DXVK's "full screen" would change the display mode
    pp->Windowed = TRUE;
    pp->EnableAutoDepthStencil = in.autoDepthStencil;
    pp->AutoDepthStencilFormat = (D3DFORMAT)in.depthStencilFormat;
    pp->Flags = in.flags;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->PresentationInterval = in.interval;
}

void FromD3D(const D3DPRESENT_PARAMETERS &pp, bo1vk_present_params *out)
{
    out->width = pp.BackBufferWidth;
    out->height = pp.BackBufferHeight;
    out->format = pp.BackBufferFormat;
    out->count = pp.BackBufferCount;
}

void *s_window;
unsigned s_backWidth, s_backHeight;

// Where the back buffer goes in the window when the window is not the back buffer's size (Android: the window is
// the display): the largest rectangle of the back buffer's shape, centered. DXVK clears the rest. The pointer
// mapping of native_video.cpp (PointerMoved) assumes the same rectangle. Null: the whole window.
const RECT *PresentRect(RECT *rect)
{
    int w = 0, h = 0;
    bo1_native_video_drawable_size(&w, &h);
    if (w <= 0 || h <= 0 || !s_backWidth || !s_backHeight || ((unsigned)w == s_backWidth && (unsigned)h == s_backHeight))
        return nullptr;
    int pw = w, ph = (int)((long long)w * s_backHeight / s_backWidth);
    if (ph > h)
    {
        ph = h;
        pw = (int)((long long)h * s_backWidth / s_backHeight);
    }
    rect->left = (w - pw) / 2;
    rect->top = (h - ph) / 2;
    rect->right = rect->left + pw;
    rect->bottom = rect->top + ph;
    return rect;
}

// bytes of one block and the block's edge in pixels (4 for the compressed formats, 1 otherwise); 0 = not known
unsigned BlockBytes(D3DFORMAT format, unsigned *edge)
{
    *edge = 1;
    switch ((unsigned)format)
    {
    case D3DFMT_DXT1:
    case MAKEFOURCC('A', 'T', 'I', '1'):
        *edge = 4;
        return 8;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
    case MAKEFOURCC('A', 'T', 'I', '2'):
        *edge = 4;
        return 16;
    case D3DFMT_A8:
    case D3DFMT_L8:
    case D3DFMT_P8:
    case D3DFMT_A4L4:
    case D3DFMT_R3G3B2:
        return 1;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_A8L8:
    case D3DFMT_A8P8:
    case D3DFMT_L16:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1:
    case D3DFMT_R16F:
    case D3DFMT_INDEX16:
        return 2;
    case D3DFMT_R8G8B8:
        return 3;
    case D3DFMT_A16B16G16R16:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_Q16W16V16U16:
    case D3DFMT_G32R32F:
        return 8;
    case D3DFMT_A32B32G32R32F:
        return 16;
    default:
        return 4;   // the 32-bit color, depth and float formats
    }
}

// bytes from the first locked byte to the last, for a region of width x height pixels and rows `pitch` apart
uint64_t RegionBytes(D3DFORMAT format, unsigned width, unsigned height, unsigned pitch)
{
    unsigned edge = 1;
    const unsigned block = BlockBytes(format, &edge);
    const unsigned rows = (height + edge - 1) / edge, columns = (width + edge - 1) / edge;
    if (!rows || !columns)
        return 0;
    return (uint64_t)(rows - 1) * pitch + (uint64_t)columns * block;
}
}

namespace
{
// Whether DXVK can make a device here, tried once with a small back buffer on the game's window. The list of
// adapters is not enough: a driver can report Vulkan 1.3 and lack an extension DXVK requires (VK_EXT_robustness2
// on the Android emulator), which only shows when the device is created. On failure the window is closed again, so
// that the OpenGL ES renderer can make its own.
bool DeviceWorks(IDirect3D9 *d3d)
{
    static int works = -1;
    if (works >= 0)
        return works != 0;
    works = 0;
    void *window = bo1_native_video_vulkan_window(1280, 720);
    if (!window)
        return false;
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (HWND)window;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *device = nullptr;
    HRESULT hr;
    try
    {
        hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    }
    catch (...)
    {
        hr = D3DERR_NOTAVAILABLE;
    }
    if (SUCCEEDED(hr) && device)
    {
        device->Release();
        works = 1;
        return true;
    }
    fprintf(stderr, "vulkan: DXVK cannot create a device with this driver (0x%08x)\n", (unsigned)hr);
    bo1_native_video_vulkan_close();
    return false;
}
}

extern "C" int bo1vk_available(void)
{
    if (s_tried)
        return s_create != nullptr;
    s_tried = true;
    // DXVK's window system for a native build; SDL2 is what native_video.cpp makes the window with
    setenv("DXVK_WSI_DRIVER", "SDL2", 0);
    // DXVK's log goes to stderr only (the program's log); by default it would also write <program>_d3d9.log into the
    // working directory, which is the game folder
    setenv("DXVK_LOG_PATH", "none", 0);
    const char *path = getenv("BO1_DXVK_LIB");
    if (!path || !*path)
        path = "libdxvk_d3d9.so";
    void *lib = nullptr;
#ifndef __ANDROID__
    // next to the program first (Android: the loader finds it in the app's library directory by name)
    if (!strchr(path, '/'))
    {
        char beside[1024];
        const ssize_t length = readlink("/proc/self/exe", beside, sizeof(beside) - 1);
        if (length > 0)
        {
            beside[length] = 0;
            char *slash = strrchr(beside, '/');
            if (slash && (size_t)(slash - beside) + strlen(path) + 2 < sizeof(beside))
            {
                strcpy(slash + 1, path);
                lib = dlopen(beside, RTLD_NOW | RTLD_LOCAL);
            }
        }
    }
#endif
    if (!lib)
        lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        fprintf(stderr, "vulkan: %s\n", dlerror());
        return 0;
    }
    bo1_lowmem_scan_host_modules();
    s_create = (CreateFn)dlsym(lib, "Direct3DCreate9");
    if (!s_create)
        fprintf(stderr, "vulkan: %s has no Direct3DCreate9\n", path);
    return s_create != nullptr;
}

extern "C" bo1vk_h bo1vk_create(void)
{
    if (!bo1vk_available() || !bo1_native_video_vulkan_prepare())
        return 0;
    // DXVK throws when there is no Vulkan driver or no usable device
    try
    {
        IDirect3D9 *d3d = s_create(D3D_SDK_VERSION);
        bo1_lowmem_scan_host_modules();   // the Vulkan loader and driver are in now
        // DXVK lists only the devices it can use (Vulkan 1.3 and the features it requires)
        if (d3d && d3d->GetAdapterCount() == 0)
        {
            fprintf(stderr, "vulkan: no Vulkan device DXVK can use (it needs Vulkan 1.3)\n");
            d3d->Release();
            d3d = nullptr;
        }
        if (d3d && !DeviceWorks(d3d))
        {
            d3d->Release();
            d3d = nullptr;
        }
        return (bo1vk_h)(uintptr_t)d3d;
    }
    catch (...)
    {
        fprintf(stderr, "vulkan: DXVK could not start (no Vulkan driver or device?)\n");
        return 0;
    }
}

extern "C" uint32_t bo1vk_addref(bo1vk_h object) { return As<IUnknown>(object)->AddRef(); }
extern "C" uint32_t bo1vk_release(bo1vk_h object) { return As<IUnknown>(object)->Release(); }
extern "C" void bo1vk_copy(bo1vk_p dst, bo1vk_p src, uint64_t bytes) { memcpy(At<void>(dst), At<void>(src), bytes); }

// ---- IDirect3D9
extern "C" uint32_t bo1vk_d3d_adapter_count(bo1vk_h d3d) { return D3D->GetAdapterCount(); }
extern "C" int32_t bo1vk_d3d_adapter_identifier(bo1vk_h d3d, uint32_t adapter, uint32_t flags, bo1vk_p identifier)
{
    return D3D->GetAdapterIdentifier(adapter, flags, At<D3DADAPTER_IDENTIFIER9>(identifier));
}
extern "C" uint32_t bo1vk_d3d_mode_count(bo1vk_h d3d, uint32_t adapter, uint32_t format)
{
    return D3D->GetAdapterModeCount(adapter, (D3DFORMAT)format);
}
extern "C" int32_t bo1vk_d3d_enum_modes(bo1vk_h d3d, uint32_t adapter, uint32_t format, uint32_t mode, bo1vk_p displayMode)
{
    return D3D->EnumAdapterModes(adapter, (D3DFORMAT)format, mode, At<D3DDISPLAYMODE>(displayMode));
}
extern "C" int32_t bo1vk_d3d_display_mode(bo1vk_h d3d, uint32_t adapter, bo1vk_p displayMode)
{
    return D3D->GetAdapterDisplayMode(adapter, At<D3DDISPLAYMODE>(displayMode));
}
extern "C" int32_t bo1vk_d3d_check_type(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat,
    uint32_t backBufferFormat, int32_t windowed)
{
    return D3D->CheckDeviceType(adapter, (D3DDEVTYPE)devType, (D3DFORMAT)adapterFormat, (D3DFORMAT)backBufferFormat, windowed);
}
extern "C" int32_t bo1vk_d3d_check_format(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat,
    uint32_t usage, uint32_t resourceType, uint32_t format)
{
    return D3D->CheckDeviceFormat(adapter, (D3DDEVTYPE)devType, (D3DFORMAT)adapterFormat, usage,
        (D3DRESOURCETYPE)resourceType, (D3DFORMAT)format);
}
extern "C" int32_t bo1vk_d3d_check_multisample(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t format,
    int32_t windowed, uint32_t type, bo1vk_p qualityLevels)
{
    return D3D->CheckDeviceMultiSampleType(adapter, (D3DDEVTYPE)devType, (D3DFORMAT)format, windowed,
        (D3DMULTISAMPLE_TYPE)type, At<DWORD>(qualityLevels));
}
extern "C" int32_t bo1vk_d3d_check_depth(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat,
    uint32_t renderTargetFormat, uint32_t depthFormat)
{
    return D3D->CheckDepthStencilMatch(adapter, (D3DDEVTYPE)devType, (D3DFORMAT)adapterFormat,
        (D3DFORMAT)renderTargetFormat, (D3DFORMAT)depthFormat);
}
extern "C" int32_t bo1vk_d3d_check_conversion(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t source,
    uint32_t target)
{
    return D3D->CheckDeviceFormatConversion(adapter, (D3DDEVTYPE)devType, (D3DFORMAT)source, (D3DFORMAT)target);
}
extern "C" int32_t bo1vk_d3d_caps(bo1vk_h d3d, uint32_t adapter, uint32_t devType, bo1vk_p caps)
{
    return D3D->GetDeviceCaps(adapter, (D3DDEVTYPE)devType, At<D3DCAPS9>(caps));
}
extern "C" int32_t bo1vk_d3d_create_device(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t behavior,
    bo1vk_p params, bo1vk_p outDevice)
{
    bo1vk_present_params *in = At<bo1vk_present_params>(params);
    s_window = bo1_native_video_vulkan_window((int)in->width, (int)in->height);
    if (!s_window)
    {
        *At<bo1vk_h>(outDevice) = 0;
        return (int32_t)D3DERR_NOTAVAILABLE;
    }
    D3DPRESENT_PARAMETERS pp;
    ToD3D(*in, s_window, &pp);
    IDirect3DDevice9 *device = nullptr;
    HRESULT hr;
    try
    {
        hr = D3D->CreateDevice(adapter, (D3DDEVTYPE)devType, (HWND)s_window, behavior, &pp, &device);
    }
    catch (...)
    {
        fprintf(stderr, "vulkan: DXVK threw while creating the device\n");
        hr = D3DERR_NOTAVAILABLE;
    }
    bo1_lowmem_scan_host_modules();
    if (SUCCEEDED(hr))
    {
        FromD3D(pp, in);
        s_backWidth = pp.BackBufferWidth;
        s_backHeight = pp.BackBufferHeight;
    }
    return Out(hr, device, outDevice);
}

// ---- IDirect3DDevice9
extern "C" int32_t bo1vk_dev_test_cooperative(bo1vk_h dev) { return DEV->TestCooperativeLevel(); }
extern "C" uint32_t bo1vk_dev_texture_mem(bo1vk_h dev) { return DEV->GetAvailableTextureMem(); }
extern "C" int32_t bo1vk_dev_evict(bo1vk_h dev) { return DEV->EvictManagedResources(); }
extern "C" int32_t bo1vk_dev_caps(bo1vk_h dev, bo1vk_p caps) { return DEV->GetDeviceCaps(At<D3DCAPS9>(caps)); }
extern "C" int32_t bo1vk_dev_display_mode(bo1vk_h dev, uint32_t swapChain, bo1vk_p displayMode)
{
    return DEV->GetDisplayMode(swapChain, At<D3DDISPLAYMODE>(displayMode));
}
extern "C" int32_t bo1vk_dev_reset(bo1vk_h dev, bo1vk_p params)
{
    bo1vk_present_params *in = At<bo1vk_present_params>(params);
    bo1_native_video_vulkan_window((int)in->width, (int)in->height);
    D3DPRESENT_PARAMETERS pp;
    ToD3D(*in, s_window, &pp);
    const HRESULT hr = DEV->Reset(&pp);
    if (SUCCEEDED(hr))
    {
        FromD3D(pp, in);
        s_backWidth = pp.BackBufferWidth;
        s_backHeight = pp.BackBufferHeight;
    }
    return hr;
}
extern "C" int32_t bo1vk_dev_present(bo1vk_h dev)
{
    RECT rect;
    const HRESULT hr = DEV->Present(nullptr, PresentRect(&rect), nullptr, nullptr);
    bo1_native_video_frame();
    return hr;
}
extern "C" int32_t bo1vk_dev_swapchain(bo1vk_h dev, uint32_t index, bo1vk_p outSwapChain)
{
    IDirect3DSwapChain9 *chain = nullptr;
    return Out(DEV->GetSwapChain(index, &chain), chain, outSwapChain);
}
extern "C" int32_t bo1vk_dev_back_buffer(bo1vk_h dev, uint32_t swapChain, uint32_t index, bo1vk_p outSurface)
{
    IDirect3DSurface9 *surface = nullptr;
    return Out(DEV->GetBackBuffer(swapChain, index, D3DBACKBUFFER_TYPE_MONO, &surface), surface, outSurface);
}
extern "C" int32_t bo1vk_dev_raster_status(bo1vk_h dev, uint32_t swapChain, bo1vk_p status)
{
    return DEV->GetRasterStatus(swapChain, At<D3DRASTER_STATUS>(status));
}
extern "C" void bo1vk_dev_set_gamma(bo1vk_h dev, uint32_t swapChain, uint32_t flags, bo1vk_p ramp)
{
    DEV->SetGammaRamp(swapChain, flags, At<D3DGAMMARAMP>(ramp));
}
extern "C" int32_t bo1vk_dev_create_texture(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t levels,
    uint32_t usage, uint32_t format, uint32_t pool, bo1vk_p out)
{
    IDirect3DTexture9 *texture = nullptr;
    return Out(DEV->CreateTexture(width, height, levels, usage, (D3DFORMAT)format, (D3DPOOL)pool, &texture, nullptr),
        texture, out);
}
extern "C" int32_t bo1vk_dev_create_volume(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t depth,
    uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool, bo1vk_p out)
{
    IDirect3DVolumeTexture9 *texture = nullptr;
    return Out(DEV->CreateVolumeTexture(width, height, depth, levels, usage, (D3DFORMAT)format, (D3DPOOL)pool,
        &texture, nullptr), texture, out);
}
extern "C" int32_t bo1vk_dev_create_cube(bo1vk_h dev, uint32_t edge, uint32_t levels, uint32_t usage, uint32_t format,
    uint32_t pool, bo1vk_p out)
{
    IDirect3DCubeTexture9 *texture = nullptr;
    return Out(DEV->CreateCubeTexture(edge, levels, usage, (D3DFORMAT)format, (D3DPOOL)pool, &texture, nullptr),
        texture, out);
}
extern "C" int32_t bo1vk_dev_create_vb(bo1vk_h dev, uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool,
    bo1vk_p out)
{
    IDirect3DVertexBuffer9 *buffer = nullptr;
    return Out(DEV->CreateVertexBuffer(length, usage, fvf, (D3DPOOL)pool, &buffer, nullptr), buffer, out);
}
extern "C" int32_t bo1vk_dev_create_ib(bo1vk_h dev, uint32_t length, uint32_t usage, uint32_t format, uint32_t pool,
    bo1vk_p out)
{
    IDirect3DIndexBuffer9 *buffer = nullptr;
    return Out(DEV->CreateIndexBuffer(length, usage, (D3DFORMAT)format, (D3DPOOL)pool, &buffer, nullptr), buffer, out);
}
extern "C" int32_t bo1vk_dev_create_rt(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format,
    uint32_t multisample, uint32_t quality, int32_t lockable, bo1vk_p out)
{
    IDirect3DSurface9 *surface = nullptr;
    return Out(DEV->CreateRenderTarget(width, height, (D3DFORMAT)format, (D3DMULTISAMPLE_TYPE)multisample, quality,
        lockable, &surface, nullptr), surface, out);
}
extern "C" int32_t bo1vk_dev_create_ds(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format,
    uint32_t multisample, uint32_t quality, int32_t discard, bo1vk_p out)
{
    IDirect3DSurface9 *surface = nullptr;
    return Out(DEV->CreateDepthStencilSurface(width, height, (D3DFORMAT)format, (D3DMULTISAMPLE_TYPE)multisample,
        quality, discard, &surface, nullptr), surface, out);
}
extern "C" int32_t bo1vk_dev_create_plain(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format, uint32_t pool,
    bo1vk_p out)
{
    IDirect3DSurface9 *surface = nullptr;
    return Out(DEV->CreateOffscreenPlainSurface(width, height, (D3DFORMAT)format, (D3DPOOL)pool, &surface, nullptr),
        surface, out);
}
extern "C" int32_t bo1vk_dev_update_surface(bo1vk_h dev, bo1vk_h src, bo1vk_p srcRect, bo1vk_h dst, bo1vk_p dstPoint)
{
    return DEV->UpdateSurface(As<IDirect3DSurface9>(src), At<RECT>(srcRect), As<IDirect3DSurface9>(dst), At<POINT>(dstPoint));
}
extern "C" int32_t bo1vk_dev_update_texture(bo1vk_h dev, bo1vk_h src, bo1vk_h dst)
{
    return DEV->UpdateTexture(As<IDirect3DBaseTexture9>(src), As<IDirect3DBaseTexture9>(dst));
}
extern "C" int32_t bo1vk_dev_rt_data(bo1vk_h dev, bo1vk_h renderTarget, bo1vk_h dst)
{
    return DEV->GetRenderTargetData(As<IDirect3DSurface9>(renderTarget), As<IDirect3DSurface9>(dst));
}
extern "C" int32_t bo1vk_dev_front_buffer_data(bo1vk_h dev, uint32_t swapChain, bo1vk_h dst)
{
    return DEV->GetFrontBufferData(swapChain, As<IDirect3DSurface9>(dst));
}
extern "C" int32_t bo1vk_dev_stretch_rect(bo1vk_h dev, bo1vk_h src, bo1vk_p srcRect, bo1vk_h dst, bo1vk_p dstRect,
    uint32_t filter)
{
    return DEV->StretchRect(As<IDirect3DSurface9>(src), At<RECT>(srcRect), As<IDirect3DSurface9>(dst), At<RECT>(dstRect),
        (D3DTEXTUREFILTERTYPE)filter);
}
extern "C" int32_t bo1vk_dev_color_fill(bo1vk_h dev, bo1vk_h surface, bo1vk_p rect, uint32_t color)
{
    return DEV->ColorFill(As<IDirect3DSurface9>(surface), At<RECT>(rect), color);
}
extern "C" int32_t bo1vk_dev_set_rt(bo1vk_h dev, uint32_t index, bo1vk_h surface)
{
    return DEV->SetRenderTarget(index, As<IDirect3DSurface9>(surface));
}
extern "C" int32_t bo1vk_dev_set_ds(bo1vk_h dev, bo1vk_h surface)
{
    return DEV->SetDepthStencilSurface(As<IDirect3DSurface9>(surface));
}
extern "C" int32_t bo1vk_dev_begin_scene(bo1vk_h dev) { return DEV->BeginScene(); }
extern "C" int32_t bo1vk_dev_end_scene(bo1vk_h dev) { return DEV->EndScene(); }
extern "C" int32_t bo1vk_dev_clear(bo1vk_h dev, uint32_t count, bo1vk_p rects, uint32_t flags, uint32_t color, float z,
    uint32_t stencil)
{
    return DEV->Clear(count, At<D3DRECT>(rects), flags, color, z, stencil);
}
extern "C" int32_t bo1vk_dev_set_transform(bo1vk_h dev, uint32_t state, bo1vk_p matrix)
{
    return DEV->SetTransform((D3DTRANSFORMSTATETYPE)state, At<D3DMATRIX>(matrix));
}
extern "C" int32_t bo1vk_dev_set_viewport(bo1vk_h dev, bo1vk_p viewport)
{
    return DEV->SetViewport(At<D3DVIEWPORT9>(viewport));
}
extern "C" int32_t bo1vk_dev_get_viewport(bo1vk_h dev, bo1vk_p viewport)
{
    return DEV->GetViewport(At<D3DVIEWPORT9>(viewport));
}
extern "C" int32_t bo1vk_dev_set_clip_plane(bo1vk_h dev, uint32_t index, bo1vk_p plane)
{
    return DEV->SetClipPlane(index, At<float>(plane));
}
extern "C" int32_t bo1vk_dev_set_render_state(bo1vk_h dev, uint32_t state, uint32_t value)
{
    return DEV->SetRenderState((D3DRENDERSTATETYPE)state, value);
}
extern "C" int32_t bo1vk_dev_get_render_state(bo1vk_h dev, uint32_t state, bo1vk_p value)
{
    return DEV->GetRenderState((D3DRENDERSTATETYPE)state, At<DWORD>(value));
}
extern "C" int32_t bo1vk_dev_set_texture(bo1vk_h dev, uint32_t stage, bo1vk_h texture)
{
    return DEV->SetTexture(stage, As<IDirect3DBaseTexture9>(texture));
}
extern "C" int32_t bo1vk_dev_set_stage_state(bo1vk_h dev, uint32_t stage, uint32_t type, uint32_t value)
{
    return DEV->SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)type, value);
}
extern "C" int32_t bo1vk_dev_set_sampler_state(bo1vk_h dev, uint32_t sampler, uint32_t type, uint32_t value)
{
    return DEV->SetSamplerState(sampler, (D3DSAMPLERSTATETYPE)type, value);
}
extern "C" int32_t bo1vk_dev_set_scissor(bo1vk_h dev, bo1vk_p rect) { return DEV->SetScissorRect(At<RECT>(rect)); }
extern "C" int32_t bo1vk_dev_draw(bo1vk_h dev, uint32_t type, uint32_t start, uint32_t count)
{
    return DEV->DrawPrimitive((D3DPRIMITIVETYPE)type, start, count);
}
extern "C" int32_t bo1vk_dev_draw_indexed(bo1vk_h dev, uint32_t type, int32_t baseVertex, uint32_t minIndex,
    uint32_t vertices, uint32_t startIndex, uint32_t count)
{
    return DEV->DrawIndexedPrimitive((D3DPRIMITIVETYPE)type, baseVertex, minIndex, vertices, startIndex, count);
}
extern "C" int32_t bo1vk_dev_draw_up(bo1vk_h dev, uint32_t type, uint32_t count, bo1vk_p data, uint32_t stride)
{
    return DEV->DrawPrimitiveUP((D3DPRIMITIVETYPE)type, count, At<void>(data), stride);
}
extern "C" int32_t bo1vk_dev_draw_indexed_up(bo1vk_h dev, uint32_t type, uint32_t minIndex, uint32_t vertices,
    uint32_t count, bo1vk_p indices, uint32_t indexFormat, bo1vk_p data, uint32_t stride)
{
    return DEV->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)type, minIndex, vertices, count, At<void>(indices),
        (D3DFORMAT)indexFormat, At<void>(data), stride);
}
extern "C" int32_t bo1vk_dev_create_decl(bo1vk_h dev, bo1vk_p elements, bo1vk_p out)
{
    IDirect3DVertexDeclaration9 *decl = nullptr;
    return Out(DEV->CreateVertexDeclaration(At<D3DVERTEXELEMENT9>(elements), &decl), decl, out);
}
extern "C" int32_t bo1vk_dev_set_decl(bo1vk_h dev, bo1vk_h decl)
{
    return DEV->SetVertexDeclaration(As<IDirect3DVertexDeclaration9>(decl));
}
extern "C" int32_t bo1vk_dev_set_fvf(bo1vk_h dev, uint32_t fvf) { return DEV->SetFVF(fvf); }
extern "C" int32_t bo1vk_dev_create_vs(bo1vk_h dev, bo1vk_p function, bo1vk_p out)
{
    IDirect3DVertexShader9 *shader = nullptr;
    return Out(DEV->CreateVertexShader(At<DWORD>(function), &shader), shader, out);
}
extern "C" int32_t bo1vk_dev_set_vs(bo1vk_h dev, bo1vk_h shader)
{
    return DEV->SetVertexShader(As<IDirect3DVertexShader9>(shader));
}
extern "C" int32_t bo1vk_dev_create_ps(bo1vk_h dev, bo1vk_p function, bo1vk_p out)
{
    IDirect3DPixelShader9 *shader = nullptr;
    return Out(DEV->CreatePixelShader(At<DWORD>(function), &shader), shader, out);
}
extern "C" int32_t bo1vk_dev_set_ps(bo1vk_h dev, bo1vk_h shader)
{
    return DEV->SetPixelShader(As<IDirect3DPixelShader9>(shader));
}
extern "C" int32_t bo1vk_dev_set_vs_const(bo1vk_h dev, int which, uint32_t start, bo1vk_p data, uint32_t count)
{
    if (which == 'F')
        return DEV->SetVertexShaderConstantF(start, At<float>(data), count);
    if (which == 'I')
        return DEV->SetVertexShaderConstantI(start, At<int>(data), count);
    return DEV->SetVertexShaderConstantB(start, At<BOOL>(data), count);
}
extern "C" int32_t bo1vk_dev_set_ps_const(bo1vk_h dev, int which, uint32_t start, bo1vk_p data, uint32_t count)
{
    if (which == 'F')
        return DEV->SetPixelShaderConstantF(start, At<float>(data), count);
    if (which == 'I')
        return DEV->SetPixelShaderConstantI(start, At<int>(data), count);
    return DEV->SetPixelShaderConstantB(start, At<BOOL>(data), count);
}
extern "C" int32_t bo1vk_dev_set_stream(bo1vk_h dev, uint32_t stream, bo1vk_h buffer, uint32_t offset, uint32_t stride)
{
    return DEV->SetStreamSource(stream, As<IDirect3DVertexBuffer9>(buffer), offset, stride);
}
extern "C" int32_t bo1vk_dev_set_stream_freq(bo1vk_h dev, uint32_t stream, uint32_t setting)
{
    return DEV->SetStreamSourceFreq(stream, setting);
}
extern "C" int32_t bo1vk_dev_set_indices(bo1vk_h dev, bo1vk_h buffer)
{
    return DEV->SetIndices(As<IDirect3DIndexBuffer9>(buffer));
}
extern "C" int32_t bo1vk_dev_create_query(bo1vk_h dev, uint32_t type, bo1vk_p out)
{
    if (!out)
        return DEV->CreateQuery((D3DQUERYTYPE)type, nullptr);
    IDirect3DQuery9 *query = nullptr;
    return Out(DEV->CreateQuery((D3DQUERYTYPE)type, &query), query, out);
}

// ---- swap chain
extern "C" int32_t bo1vk_swap_present(bo1vk_h swapChain, uint32_t flags)
{
    RECT rect;
    const HRESULT hr = As<IDirect3DSwapChain9>(swapChain)->Present(nullptr, PresentRect(&rect), nullptr, nullptr, flags);
    bo1_native_video_frame();
    return hr;
}
extern "C" int32_t bo1vk_swap_back_buffer(bo1vk_h swapChain, uint32_t index, bo1vk_p outSurface)
{
    IDirect3DSurface9 *surface = nullptr;
    return Out(As<IDirect3DSwapChain9>(swapChain)->GetBackBuffer(index, D3DBACKBUFFER_TYPE_MONO, &surface), surface,
        outSurface);
}
extern "C" int32_t bo1vk_swap_front_buffer_data(bo1vk_h swapChain, bo1vk_h dst)
{
    return As<IDirect3DSwapChain9>(swapChain)->GetFrontBufferData(As<IDirect3DSurface9>(dst));
}

// ---- textures, surfaces
extern "C" uint32_t bo1vk_tex_level_count(bo1vk_h texture) { return As<IDirect3DBaseTexture9>(texture)->GetLevelCount(); }
extern "C" void bo1vk_tex_generate_mips(bo1vk_h texture) { As<IDirect3DBaseTexture9>(texture)->GenerateMipSubLevels(); }
extern "C" void bo1vk_res_preload(bo1vk_h resource) { As<IDirect3DResource9>(resource)->PreLoad(); }

extern "C" int32_t bo1vk_tex_desc(bo1vk_h object, int kind, uint32_t level, bo1vk_p desc)
{
    switch (kind)
    {
    case BO1VK_TEX2D:
        return As<IDirect3DTexture9>(object)->GetLevelDesc(level, At<D3DSURFACE_DESC>(desc));
    case BO1VK_CUBE:
        return As<IDirect3DCubeTexture9>(object)->GetLevelDesc(level, At<D3DSURFACE_DESC>(desc));
    case BO1VK_SURFACE:
        return As<IDirect3DSurface9>(object)->GetDesc(At<D3DSURFACE_DESC>(desc));
    default:
        return As<IDirect3DVolumeTexture9>(object)->GetLevelDesc(level, At<D3DVOLUME_DESC>(desc));
    }
}

extern "C" int32_t bo1vk_tex_surface(bo1vk_h texture, int kind, uint32_t face, uint32_t level, bo1vk_p outSurface)
{
    IDirect3DSurface9 *surface = nullptr;
    if (kind == BO1VK_CUBE)
        return Out(As<IDirect3DCubeTexture9>(texture)->GetCubeMapSurface((D3DCUBEMAP_FACES)face, level, &surface),
            surface, outSurface);
    return Out(As<IDirect3DTexture9>(texture)->GetSurfaceLevel(level, &surface), surface, outSurface);
}

extern "C" int32_t bo1vk_tex_lock(bo1vk_h object, int kind, uint32_t face, uint32_t level, bo1vk_p rect, uint32_t flags,
    bo1vk_p outPitch, bo1vk_p outBits, bo1vk_p outBytes)
{
    D3DSURFACE_DESC desc;
    HRESULT hr = (HRESULT)bo1vk_tex_desc(object, kind, level, (bo1vk_p)(uintptr_t)&desc);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT locked = {};
    const RECT *r = At<RECT>(rect);
    if (kind == BO1VK_TEX2D)
        hr = As<IDirect3DTexture9>(object)->LockRect(level, &locked, r, flags);
    else if (kind == BO1VK_CUBE)
        hr = As<IDirect3DCubeTexture9>(object)->LockRect((D3DCUBEMAP_FACES)face, level, &locked, r, flags);
    else
        hr = As<IDirect3DSurface9>(object)->LockRect(&locked, r, flags);
    if (FAILED(hr))
        return hr;
    const unsigned width = r ? (unsigned)(r->right - r->left) : desc.Width;
    const unsigned height = r ? (unsigned)(r->bottom - r->top) : desc.Height;
    *At<int32_t>(outPitch) = locked.Pitch;
    *At<uint64_t>(outBits) = (uint64_t)(uintptr_t)locked.pBits;
    *At<uint64_t>(outBytes) = RegionBytes(desc.Format, width, height, (unsigned)locked.Pitch);
    return hr;
}

extern "C" int32_t bo1vk_tex_unlock(bo1vk_h object, int kind, uint32_t face, uint32_t level)
{
    if (kind == BO1VK_TEX2D)
        return As<IDirect3DTexture9>(object)->UnlockRect(level);
    if (kind == BO1VK_CUBE)
        return As<IDirect3DCubeTexture9>(object)->UnlockRect((D3DCUBEMAP_FACES)face, level);
    return As<IDirect3DSurface9>(object)->UnlockRect();
}

extern "C" int32_t bo1vk_tex_dirty(bo1vk_h texture, int kind, uint32_t face, bo1vk_p rect)
{
    if (kind == BO1VK_CUBE)
        return As<IDirect3DCubeTexture9>(texture)->AddDirtyRect((D3DCUBEMAP_FACES)face, At<RECT>(rect));
    return As<IDirect3DTexture9>(texture)->AddDirtyRect(At<RECT>(rect));
}

extern "C" int32_t bo1vk_vol_lock(bo1vk_h texture, uint32_t level, bo1vk_p box, uint32_t flags, bo1vk_p outRowPitch,
    bo1vk_p outSlicePitch, bo1vk_p outBits, bo1vk_p outBytes)
{
    IDirect3DVolumeTexture9 *volume = As<IDirect3DVolumeTexture9>(texture);
    D3DVOLUME_DESC desc;
    HRESULT hr = volume->GetLevelDesc(level, &desc);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_BOX locked = {};
    const D3DBOX *b = At<D3DBOX>(box);
    hr = volume->LockBox(level, &locked, b, flags);
    if (FAILED(hr))
        return hr;
    const unsigned width = b ? b->Right - b->Left : desc.Width, height = b ? b->Bottom - b->Top : desc.Height;
    const unsigned depth = b ? b->Back - b->Front : desc.Depth;
    *At<int32_t>(outRowPitch) = locked.RowPitch;
    *At<int32_t>(outSlicePitch) = locked.SlicePitch;
    *At<uint64_t>(outBits) = (uint64_t)(uintptr_t)locked.pBits;
    *At<uint64_t>(outBytes) = depth ? (uint64_t)(depth - 1) * (unsigned)locked.SlicePitch
        + RegionBytes(desc.Format, width, height, (unsigned)locked.RowPitch) : 0;
    return hr;
}

extern "C" int32_t bo1vk_vol_unlock(bo1vk_h texture, uint32_t level)
{
    return As<IDirect3DVolumeTexture9>(texture)->UnlockBox(level);
}

// ---- vertex / index buffers
extern "C" int32_t bo1vk_buf_lock(bo1vk_h buffer, int index, uint32_t offset, uint32_t size, uint32_t flags,
    bo1vk_p outData)
{
    void *data = nullptr;
    const HRESULT hr = index ? As<IDirect3DIndexBuffer9>(buffer)->Lock(offset, size, &data, flags)
                             : As<IDirect3DVertexBuffer9>(buffer)->Lock(offset, size, &data, flags);
    *At<uint64_t>(outData) = (uint64_t)(uintptr_t)data;
    return hr;
}

extern "C" int32_t bo1vk_buf_unlock(bo1vk_h buffer, int index)
{
    return index ? As<IDirect3DIndexBuffer9>(buffer)->Unlock() : As<IDirect3DVertexBuffer9>(buffer)->Unlock();
}

// ---- queries
extern "C" int32_t bo1vk_query_issue(bo1vk_h query, uint32_t flags) { return As<IDirect3DQuery9>(query)->Issue(flags); }
extern "C" int32_t bo1vk_query_data(bo1vk_h query, bo1vk_p data, uint32_t size, uint32_t flags)
{
    return As<IDirect3DQuery9>(query)->GetData(At<void>(data), size, flags);
}
extern "C" uint32_t bo1vk_query_data_size(bo1vk_h query) { return As<IDirect3DQuery9>(query)->GetDataSize(); }

#endif // BO1_DXVK
