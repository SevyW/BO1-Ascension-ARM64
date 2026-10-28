# STATUS: Turnip (custom Vulkan driver) build of the BO1 Android port

Branch `turnip`. Updated by the cloud session at every milestone.

## Current phase

**Phase 1 (reproduce the official build), in progress.** The Phase 2 code is written and committed, but not yet built.

## Build environment (cloud container, Ubuntu 24.04, 4 cores)

The network policy blocks `dl.google.com`, `apt.llvm.org` and `deb.debian.org`, so the Dockerfile can't be built as is.
These stand-ins give the same versions:

| Dockerfile / script expects | used here |
| --- | --- |
| Debian 13 + LLVM 21 (apt.llvm.org) | Ubuntu 24.04 packages + the official LLVM 21.1.8 release tarball from GitHub, unpacked to `/usr/lib/llvm-21` (`clang-21`, `llvm-config-21`, libc++ symlinked) |
| NDK r30 from dl.google.com (`get-ndk.sh`) | NDK 30.0.16248370 (= r30), copied out of the Docker Hub image `cimg/android:2026.10.1-ndk` |
| SDK build-tools 35.0.0 + android-35 (`sdkmanager`) | the same, from that image |
| meson (Debian 13: 1.7.0) | meson 1.7.0 from PyPI |
| arm64 system image (qemu tests only) | not available; not needed for the APK |

## Done

- NDK, SDK, LLVM 21 host toolchain; SDL2 2.32.10 source; FFmpeg n7.1 (linux, arm64-v8a).
- Phase 2 code (see below), libadrenotools built (`build-adrenotools.sh`).

## Running

- `build-llvm.sh` (patched clang 21.1.8): almost done (3100/3229 steps).
- p32 tree generated (`build-p32.sh` regeneration half). The host LLVM tarball has no `libLLVM-21.so`; `libclang-cpp.so` exports
  LLVM itself, so an empty stub `libLLVM-21.so` satisfies `-lLLVM-21`. `/bo1/src/build/` must exist before `build-p32.sh`.

## Open problems

- (resolved) The fork wasn't reachable from the cloud session at first. Since it was attached, everything is pushed here.

- `build-dxvk.sh linux` builds no library here: `ninja ... src/d3d9/libdxvk_d3d9.so` has no such target (meson names it
  `libdxvk_d3d9.so.0.20601`). That's only the desktop x64 test path, not the Android chain. Skipped.
- Most scripts under `tools/native/` are mode 644 in git, so `build-all-android.sh` can't exec them on Linux. I run them
  with `bash <script>`.

## Phase 2 design (written, not yet built)

- `src/native/android/bo1_driver.c` (new, in libmain.so): reads `files/driver.txt`, finds `files/driver/<name>/`
  (meta.json `libraryName`, or the folder's only `.so`), copies the library to internal storage (the external dir is
  noexec), `dlopen`s `libadrenotools.so` from the native lib dir, calls `adrenotools_open_libvulkan(RTLD_NOW,
  ADRENOTOOLS_DRIVER_CUSTOM, NULL, <nativeLibDir>/, <internal>/driver/<name>/, <lib>, NULL, NULL)`, makes a probe
  instance to read the device and driver properties, and exports the loader's `vkGetInstanceProcAddr` as
  `BO1_VK_GET_INSTANCE_PROC_ADDR`. Any failure falls back to the system driver. Either way it writes one
  `vulkan driver: ...` line to bo1.log.
- `tools/native/build-dxvk.sh`: new patch block `BO1_LOADER` (Android only, active only when that variable is set):
  DXVK's `loadVulkanLibrary()` uses the given `vkGetInstanceProcAddr`; the SDL2 WSI's `createSurface` builds the
  Android surface through DXVK's loader (`SDL_GetWindowWMInfo` → `ANativeWindow`, `vkCreateAndroidSurfaceKHR`), because
  `SDL_Vulkan_CreateSurface` would use SDL's system loader, which doesn't know the other loader's instance.
- `tools/native/build-adrenotools.sh` (new): libadrenotools @ 8fae8ce with the NDK, as shared libraries.
- `tools/native/build-apk.sh`: packs `libadrenotools.so` and the 4 hook libraries, plus the licence; output
  `bo1-zombies-turnip-debug.apk`.
- `android/AndroidManifest.xml`: package `org.bo1port.zombies.turnip`, label "BO1 Port Turnip", version 0.3-turnip.

## Next

Patched clang → `build-p32.sh` → `build-android.sh` → `build-dxvk.sh arm64-v8a` → `build-apk.sh` on unchanged
`main` (Phase 1 checkpoint), then the same on `turnip`.
