// bo1vk.h - the Vulkan renderer's seam: Direct3D 9 calls as plain C functions over DXVK's native D3D9
// (libdxvk_d3d9.so), for the engine side that cannot include DXVK's headers.
//
// Why a seam: in the 32-bit-pointer builds (tools/p32) the engine's D3D9 interfaces, structures and out-parameters
// hold 4-byte pointers, DXVK's hold 8-byte ones. The engine side (src/web/d3d9/d3d9_vkbridge.cpp, rewritten like
// the engine) implements the engine's IDirect3D* interfaces and calls these functions; the DXVK side
// (bo1vk_dxvk.cpp, not rewritten) calls DXVK. So nothing here has a pointer type:
//   bo1vk_h   a DXVK COM object (64-bit address, opaque to the engine side)
//   bo1vk_p   an address in the engine's memory (below 4 GB, zero-extended), or one DXVK returned (anywhere)
// Structures without pointers (D3DCAPS9, D3DVIEWPORT9, D3DSURFACE_DESC, RECT, ...) have one layout on both sides
// and go through by address. Those with pointers are passed field by field (bo1vk_present_params, lock results).
//
// HRESULTs are returned as int32_t. This file is not rewritten (tools/p32).
#ifndef BO1VK_H
#define BO1VK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t bo1vk_h;
typedef uint64_t bo1vk_p;

// D3DPRESENT_PARAMETERS without hDeviceWindow (the window is the native build's own, native_video.cpp)
typedef struct bo1vk_present_params
{
    uint32_t width, height, format, count, multisample, quality, swapEffect, windowed, autoDepthStencil,
        depthStencilFormat, flags, refreshRate, interval;
} bo1vk_present_params;

// which kind of object a lock / description call is for
enum { BO1VK_TEX2D = 0, BO1VK_CUBE = 1, BO1VK_SURFACE = 2, BO1VK_VOLUME = 3 };

// 1 when this build has the DXVK side and the library loads (libdxvk_d3d9.so next to the program, or
// BO1_DXVK_LIB=<path>); the reason goes to stderr otherwise
int bo1vk_available(void);
// DXVK's Direct3DCreate9; 0 on failure (no Vulkan driver, ...)
bo1vk_h bo1vk_create(void);
uint32_t bo1vk_addref(bo1vk_h object);
uint32_t bo1vk_release(bo1vk_h object);
// memcpy between two 64-bit addresses
void bo1vk_copy(bo1vk_p dst, bo1vk_p src, uint64_t bytes);

// ---- IDirect3D9
uint32_t bo1vk_d3d_adapter_count(bo1vk_h d3d);
int32_t bo1vk_d3d_adapter_identifier(bo1vk_h d3d, uint32_t adapter, uint32_t flags, bo1vk_p identifier);
uint32_t bo1vk_d3d_mode_count(bo1vk_h d3d, uint32_t adapter, uint32_t format);
int32_t bo1vk_d3d_enum_modes(bo1vk_h d3d, uint32_t adapter, uint32_t format, uint32_t mode, bo1vk_p displayMode);
int32_t bo1vk_d3d_display_mode(bo1vk_h d3d, uint32_t adapter, bo1vk_p displayMode);
int32_t bo1vk_d3d_check_type(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat,
    uint32_t backBufferFormat, int32_t windowed);
int32_t bo1vk_d3d_check_format(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat, uint32_t usage,
    uint32_t resourceType, uint32_t format);
int32_t bo1vk_d3d_check_multisample(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t format, int32_t windowed,
    uint32_t type, bo1vk_p qualityLevels);
int32_t bo1vk_d3d_check_depth(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t adapterFormat,
    uint32_t renderTargetFormat, uint32_t depthFormat);
int32_t bo1vk_d3d_check_conversion(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t source, uint32_t target);
int32_t bo1vk_d3d_caps(bo1vk_h d3d, uint32_t adapter, uint32_t devType, bo1vk_p caps);
// creates the window (native_video.cpp) and the device; params is a bo1vk_present_params, updated like D3D does
int32_t bo1vk_d3d_create_device(bo1vk_h d3d, uint32_t adapter, uint32_t devType, uint32_t behavior, bo1vk_p params,
    bo1vk_p outDevice);

// ---- IDirect3DDevice9
int32_t bo1vk_dev_test_cooperative(bo1vk_h dev);
uint32_t bo1vk_dev_texture_mem(bo1vk_h dev);
int32_t bo1vk_dev_evict(bo1vk_h dev);
int32_t bo1vk_dev_caps(bo1vk_h dev, bo1vk_p caps);
int32_t bo1vk_dev_display_mode(bo1vk_h dev, uint32_t swapChain, bo1vk_p displayMode);
int32_t bo1vk_dev_reset(bo1vk_h dev, bo1vk_p params);
int32_t bo1vk_dev_present(bo1vk_h dev);
int32_t bo1vk_dev_swapchain(bo1vk_h dev, uint32_t index, bo1vk_p outSwapChain);
int32_t bo1vk_dev_back_buffer(bo1vk_h dev, uint32_t swapChain, uint32_t index, bo1vk_p outSurface);
int32_t bo1vk_dev_raster_status(bo1vk_h dev, uint32_t swapChain, bo1vk_p status);
void bo1vk_dev_set_gamma(bo1vk_h dev, uint32_t swapChain, uint32_t flags, bo1vk_p ramp);
int32_t bo1vk_dev_create_texture(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
    uint32_t format, uint32_t pool, bo1vk_p out);
int32_t bo1vk_dev_create_volume(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels,
    uint32_t usage, uint32_t format, uint32_t pool, bo1vk_p out);
int32_t bo1vk_dev_create_cube(bo1vk_h dev, uint32_t edge, uint32_t levels, uint32_t usage, uint32_t format,
    uint32_t pool, bo1vk_p out);
int32_t bo1vk_dev_create_vb(bo1vk_h dev, uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool, bo1vk_p out);
int32_t bo1vk_dev_create_ib(bo1vk_h dev, uint32_t length, uint32_t usage, uint32_t format, uint32_t pool, bo1vk_p out);
int32_t bo1vk_dev_create_rt(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format, uint32_t multisample,
    uint32_t quality, int32_t lockable, bo1vk_p out);
int32_t bo1vk_dev_create_ds(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format, uint32_t multisample,
    uint32_t quality, int32_t discard, bo1vk_p out);
int32_t bo1vk_dev_create_plain(bo1vk_h dev, uint32_t width, uint32_t height, uint32_t format, uint32_t pool,
    bo1vk_p out);
int32_t bo1vk_dev_update_surface(bo1vk_h dev, bo1vk_h src, bo1vk_p srcRect, bo1vk_h dst, bo1vk_p dstPoint);
int32_t bo1vk_dev_update_texture(bo1vk_h dev, bo1vk_h src, bo1vk_h dst);
int32_t bo1vk_dev_rt_data(bo1vk_h dev, bo1vk_h renderTarget, bo1vk_h dst);
int32_t bo1vk_dev_front_buffer_data(bo1vk_h dev, uint32_t swapChain, bo1vk_h dst);
int32_t bo1vk_dev_stretch_rect(bo1vk_h dev, bo1vk_h src, bo1vk_p srcRect, bo1vk_h dst, bo1vk_p dstRect,
    uint32_t filter);
int32_t bo1vk_dev_color_fill(bo1vk_h dev, bo1vk_h surface, bo1vk_p rect, uint32_t color);
int32_t bo1vk_dev_set_rt(bo1vk_h dev, uint32_t index, bo1vk_h surface);
int32_t bo1vk_dev_set_ds(bo1vk_h dev, bo1vk_h surface);
int32_t bo1vk_dev_begin_scene(bo1vk_h dev);
int32_t bo1vk_dev_end_scene(bo1vk_h dev);
int32_t bo1vk_dev_clear(bo1vk_h dev, uint32_t count, bo1vk_p rects, uint32_t flags, uint32_t color, float z,
    uint32_t stencil);
int32_t bo1vk_dev_set_transform(bo1vk_h dev, uint32_t state, bo1vk_p matrix);
int32_t bo1vk_dev_set_viewport(bo1vk_h dev, bo1vk_p viewport);
int32_t bo1vk_dev_get_viewport(bo1vk_h dev, bo1vk_p viewport);
int32_t bo1vk_dev_set_clip_plane(bo1vk_h dev, uint32_t index, bo1vk_p plane);
int32_t bo1vk_dev_set_render_state(bo1vk_h dev, uint32_t state, uint32_t value);
int32_t bo1vk_dev_get_render_state(bo1vk_h dev, uint32_t state, bo1vk_p value);
int32_t bo1vk_dev_set_texture(bo1vk_h dev, uint32_t stage, bo1vk_h texture);
int32_t bo1vk_dev_set_stage_state(bo1vk_h dev, uint32_t stage, uint32_t type, uint32_t value);
int32_t bo1vk_dev_set_sampler_state(bo1vk_h dev, uint32_t sampler, uint32_t type, uint32_t value);
int32_t bo1vk_dev_set_scissor(bo1vk_h dev, bo1vk_p rect);
int32_t bo1vk_dev_draw(bo1vk_h dev, uint32_t type, uint32_t start, uint32_t count);
int32_t bo1vk_dev_draw_indexed(bo1vk_h dev, uint32_t type, int32_t baseVertex, uint32_t minIndex, uint32_t vertices,
    uint32_t startIndex, uint32_t count);
int32_t bo1vk_dev_draw_up(bo1vk_h dev, uint32_t type, uint32_t count, bo1vk_p data, uint32_t stride);
int32_t bo1vk_dev_draw_indexed_up(bo1vk_h dev, uint32_t type, uint32_t minIndex, uint32_t vertices, uint32_t count,
    bo1vk_p indices, uint32_t indexFormat, bo1vk_p data, uint32_t stride);
int32_t bo1vk_dev_create_decl(bo1vk_h dev, bo1vk_p elements, bo1vk_p out);
int32_t bo1vk_dev_set_decl(bo1vk_h dev, bo1vk_h decl);
int32_t bo1vk_dev_set_fvf(bo1vk_h dev, uint32_t fvf);
int32_t bo1vk_dev_create_vs(bo1vk_h dev, bo1vk_p function, bo1vk_p out);
int32_t bo1vk_dev_set_vs(bo1vk_h dev, bo1vk_h shader);
int32_t bo1vk_dev_create_ps(bo1vk_h dev, bo1vk_p function, bo1vk_p out);
int32_t bo1vk_dev_set_ps(bo1vk_h dev, bo1vk_h shader);
// which: 'F' float4, 'I' int4, 'B' bool
int32_t bo1vk_dev_set_vs_const(bo1vk_h dev, int which, uint32_t start, bo1vk_p data, uint32_t count);
int32_t bo1vk_dev_set_ps_const(bo1vk_h dev, int which, uint32_t start, bo1vk_p data, uint32_t count);
int32_t bo1vk_dev_set_stream(bo1vk_h dev, uint32_t stream, bo1vk_h buffer, uint32_t offset, uint32_t stride);
int32_t bo1vk_dev_set_stream_freq(bo1vk_h dev, uint32_t stream, uint32_t setting);
int32_t bo1vk_dev_set_indices(bo1vk_h dev, bo1vk_h buffer);
int32_t bo1vk_dev_create_query(bo1vk_h dev, uint32_t type, bo1vk_p out);   // out 0: only asks whether it is supported

// ---- swap chain
int32_t bo1vk_swap_present(bo1vk_h swapChain, uint32_t flags);
int32_t bo1vk_swap_back_buffer(bo1vk_h swapChain, uint32_t index, bo1vk_p outSurface);
int32_t bo1vk_swap_front_buffer_data(bo1vk_h swapChain, bo1vk_h dst);

// ---- textures, surfaces
uint32_t bo1vk_tex_level_count(bo1vk_h texture);
void bo1vk_tex_generate_mips(bo1vk_h texture);
void bo1vk_res_preload(bo1vk_h resource);
// D3DSURFACE_DESC (kind 0..2) or D3DVOLUME_DESC (kind 3)
int32_t bo1vk_tex_desc(bo1vk_h object, int kind, uint32_t level, bo1vk_p desc);
int32_t bo1vk_tex_surface(bo1vk_h texture, int kind, uint32_t face, uint32_t level, bo1vk_p outSurface);
// LockRect. *bits is DXVK's address, *bytes how many bytes from there belong to the locked rectangle (whole rows
// but the last, which ends with the rectangle).
int32_t bo1vk_tex_lock(bo1vk_h object, int kind, uint32_t face, uint32_t level, bo1vk_p rect, uint32_t flags,
    bo1vk_p outPitch, bo1vk_p outBits, bo1vk_p outBytes);
int32_t bo1vk_tex_unlock(bo1vk_h object, int kind, uint32_t face, uint32_t level);
int32_t bo1vk_tex_dirty(bo1vk_h texture, int kind, uint32_t face, bo1vk_p rect);
int32_t bo1vk_vol_lock(bo1vk_h texture, uint32_t level, bo1vk_p box, uint32_t flags, bo1vk_p outRowPitch,
    bo1vk_p outSlicePitch, bo1vk_p outBits, bo1vk_p outBytes);
int32_t bo1vk_vol_unlock(bo1vk_h texture, uint32_t level);

// ---- vertex / index buffers
int32_t bo1vk_buf_lock(bo1vk_h buffer, int index, uint32_t offset, uint32_t size, uint32_t flags, bo1vk_p outData);
int32_t bo1vk_buf_unlock(bo1vk_h buffer, int index);

// ---- queries
int32_t bo1vk_query_issue(bo1vk_h query, uint32_t flags);
int32_t bo1vk_query_data(bo1vk_h query, bo1vk_p data, uint32_t size, uint32_t flags);
uint32_t bo1vk_query_data_size(bo1vk_h query);

// ---- the native build's window for Vulkan (native_video.cpp); DXVK's SDL2 window system takes an SDL_Window
void *bo1_native_video_vulkan_window(int width, int height);
// SDL's video subsystem and Vulkan loader, before DXVK makes its instance; 0 when there is no Vulkan loader
int bo1_native_video_vulkan_prepare(void);
// once per presented frame: SDL events, game pads
void bo1_native_video_frame(void);
// destroys the Vulkan window (the Vulkan renderer turned out not to work; OpenGL ES makes its own)
void bo1_native_video_vulkan_close(void);
// the window's size in pixels (0x0 before there is one)
void bo1_native_video_drawable_size(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif // BO1VK_H
