// native_video.h - the native build's window, GL ES 3 context and input events (native_video.cpp, SDL2).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registers the SDL window / context with the D3D9 shim (d3d9shim_set_host). Nothing is created until the renderer
// asks for a device, so a dedicated server never opens a window. Call before Com_Init.
void bo1_native_video_register(void);

// Polls the window's events into the engine's input queue (src/web/web_input.cpp). A no-op before the window exists
// and on threads other than the one that created it.
void bo1_native_video_pump(void);

// Game controllers (native_gamepad.cpp): reads SDL's controllers into the bo1_gamepad array the engine's pad
// backend reads, and plays the rumble it asks for. Called once per frame on the window's thread.
void bo1_native_gamepad_update(void);

// Nonzero while the engine only reads the adapter's name at startup (web_main.cpp around Sys_FindInfo): the
// Vulkan renderer is not started for that (src/web/web_input.cpp, bo1_native_renderer_is_vulkan).
extern int bo1_native_renderer_detecting;

// x86 builds: aligned SSE/AVX moves that fault on an unaligned address are rewritten to unaligned ones
// (native_x86_unaligned.cpp explains why). A no-op on other architectures. Call first thing in main.
void bo1_native_x86_install_unaligned_fixup(void);

#ifdef __cplusplus
}
#endif
