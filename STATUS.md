# STATUS: Turnip (custom Vulkan driver) build of the BO1 Android port

Last update: 2026-10-29.

## Current state

**0.5-turnip (versionCode 5)**, branch `video-restart`: the TV menu keeps its picture after a video restart (a UI3D texture window keeps its own placement through `CL_SetupViewport` and a device reset, `src/client_mp/cl_main_mp.cpp`, `src/gfx_d3d/r_ui3d.cpp`; `ui3d:` log lines), SYNC EVERY FRAME applies through a video restart (a new Vulkan surface when the sync interval changes, `BO1_SYNC` in `tools/native/build-dxvk.sh`), and the frame rate is set only by Settings > Graphics (no EXTRAS FRAME RATE LIMIT row; `com_maxfps` 0 on a first start and once for existing configs, `bo1_maxfps_free`). Built here. First device test (PC session, Thor): the frame-rate part passed; the TV stayed blank and sync off did not apply through `vid_restart`; both changed since, second device test pending.

### 0.4-turnip

**0.4-turnip (versionCode 4)**: 0.3-turnip plus the second-screen controls ([docs/second-screen.md](docs/second-screen.md)), device-tested on the AYN Thor by the PC session. The repository is trimmed to the Android port (branch `cleanup`): the browser version, the Windows build with its Windows-only engine code, the Windows launchers, the developer tools, `mods/` and `data/` are gone (licence files kept); the state before is on the branch `archive/pre-cleanup`. Built from scratch after every removal: 1249/1249 TUs, 0 failed; the 0.4 debug APK is on the branch `v0.4-apk`. Release by the PC session.

### 0.3-turnip

**Released and device-tested.** [`v0.3-turnip`](https://github.com/SevyW/bo1-zombies-android/releases/tag/v0.3-turnip) is the code in this file's "What changed" section, built by a cloud session and tested on an AYN Thor.

| | |
| --- | --- |
| APK | `bo1-port-turnip-v0.3.apk`, on the release page |
| package | `org.bo1port.zombies.turnip`, label "BO1 Port Turnip", versionName 0.3-turnip |
| signing | permanent release key; certificate SHA-256 `6f503475137b46b5146ec057140e72ed1d7b6afc3665de253e088c7fc9ab60a3`. Builds from `build-apk.sh` are debug-signed; release APKs are re-signed with that key, so updates install over the top |
| built from | `f89909f` |

### Device test (AYN Thor, Snapdragon 8 Gen 2 / Adreno 740, Android 13, 2026-10-29)

Vulkan renderer, the Zombies main menu, 1280x720, uncapped:

| driver | `vulkan driver:` line | picture | FPS |
| --- | --- | --- | ---: |
| system (Qualcomm 0.676.53) | `system (no driver.txt)` | models flat grey, TV menu text missing | 96 |
| MrPurple T30 (Mesa 26.3.0) | `custom "t30" from driver/t30/vulkan.purple.so` | correct | 104 |
| MrPurple T28 (Mesa 26.2.0) | `custom "t28" from driver/t28/vulkan.purple.so` | correct | 86 |
| StevenMXZ v27.0.0-R1 (Mesa 27.0.0) | `custom "v27" from driver/v27/libvulkan_freedreno.so` | correct | 119 |

- No adrenotools errors in logcat.
- Kino der Toten was played on v27.
- 1920x1080 with `com_maxfps 60` holds 60 FPS on v27.
- Multiplayer hasn't been tried on Turnip yet.

### Next

- Second-screen controls: done in 0.4-turnip (one look pad, size press to fire, remapping screen).
- Picking the driver in the app instead of with `driver.txt`.

## What changed (diff to upstream 3401a66: 8 files, +331 −8)

| file | change |
| --- | --- |
| `src/native/android/bo1_driver.c` (new, in libmain.so) | `Bo1SelectVulkanDriver`: reads `files/driver.txt` (first line; empty or `system` = system driver); folder `files/driver/<name>/`, library from meta.json `libraryName` or the folder's `.so`; copies it to internal storage (`<internal>/driver/<name>/`, because external storage is noexec); `dlopen`s `<nativeLibDir>/libadrenotools.so`; `adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, NULL, <nativeLibDir>/, <internal>/driver/<name>/, <lib>, NULL, NULL)`; probe instance → device and driver properties; exports the loader's `vkGetInstanceProcAddr` as `BO1_VK_GET_INSTANCE_PROC_ADDR` (hex). Any failure: system driver. One log line either way: `vulkan driver: custom "<name>" from driver/<name>/<lib> (meta.json: "<name>", <driverVersion>): <device>, <driverName> <driverInfo> (driver version x.y.z), Vulkan x.y.z` or `vulkan driver: system (<reason>): ...` |
| `src/native/android/bo1_boot.c` | calls it once in `SDL_main`, after the log is open and the native lib dir is known (`// mod (turnip):`) |
| `cmake/native.cmake` | `bo1_driver.c` added to libmain.so (`bo1_boot` target) |
| `tools/native/build-dxvk.sh` | new patch block `BO1_LOADER`, Android only, active only when `BO1_VK_GET_INSTANCE_PROC_ADDR` is set: (1) `vulkan_loader.cpp` `loadVulkanLibrary()` takes that `vkGetInstanceProcAddr` instead of dlopen'ing `libvulkan.so`; (2) SDL2 WSI `createSurface` gets the `ANativeWindow` from `SDL_GetWindowWMInfo` and calls `vkCreateAndroidSurfaceKHR` through DXVK's loader, because `SDL_Vulkan_CreateSurface` would use SDL's system-loader `vkGetInstanceProcAddr`, which doesn't know an instance of the other loader copy. The swapchain stays the Android loader's |
| `tools/native/build-adrenotools.sh` (new) | libadrenotools (bylaws) @ 8fae8ce, NDK r30, android-28, `BUILD_SHARED_LIBS=ON`: `libadrenotools.so` + `libhook_impl.so`, `libmain_hook.so`, `libfile_redirect_hook.so`, `libgsl_alloc_hook.so` |
| `tools/native/build-apk.sh` | packs those 5 libraries and `assets/licenses/libadrenotools.txt`; output `bo1-zombies-turnip-debug.apk` |
| `tools/native/build-all-android.sh` | runs `build-adrenotools.sh` |
| `android/AndroidManifest.xml` | package `org.bo1port.zombies.turnip`, label "BO1 Port Turnip", versionName 0.3-turnip, activity named in full (`org.bo1port.zombies.Bo1Activity`, Java classes unchanged) |

The engine (`libbo1.so`) is unchanged. With no `driver.txt`, the only differences at run time are one probe instance on
the system driver at start and the log line.

## Build steps to reproduce

The repo's recipe (`tools/native/docker/Dockerfile`, then the scripts) with the repo at `/bo1/src`, NDK r30 at
`/bo1/ndk/ndk-r30/android-ndk-r30`, work folders under `/bo1`:

```
bash tools/native/get-ndk.sh          # or copy NDK r30 from elsewhere (below)
bash tools/native/build-llvm.sh       # patched clang 21.1.8, ~1 h
bash tools/native/android-setup.sh
bash tools/native/build-ffmpeg.sh arm64-v8a
mkdir -p /bo1/src/build && bash tools/native/build-p32.sh   # only its regeneration half is needed for Android
bash tools/native/build-android.sh
bash tools/native/build-dxvk.sh arm64-v8a
bash tools/native/build-adrenotools.sh
bash tools/native/build-apk.sh        # -> /bo1/android/out/bo1-zombies-turnip-debug.apk
```

Most scripts are mode 644 in git, so run them with `bash`; `build-all-android.sh` would fail to exec them on Linux.

### This container's stand-ins (no repo changes; the network blocks dl.google.com, apt.llvm.org, deb.debian.org)

| expected | used |
| --- | --- |
| Debian 13 + LLVM 21 (apt.llvm.org) | Ubuntu 24.04 + LLVM 21.1.8 release tarball from GitHub in `/usr/lib/llvm-21` (`clang-21`, `clang++-21`, `llvm-config-21` symlinks) |
| `libLLVM-21.so` | empty stub (libclang-cpp in the tarball exports LLVM itself); it and `libclang-cpp.so.21.1` symlinked into `/usr/lib/x86_64-linux-gnu` |
| Debian's libc++-21-dev at `/usr/include/c++/v1` | symlinks `/usr/include/c++/v1`, `/usr/include/x86_64-unknown-linux-gnu/c++/v1` → the tarball's headers. **Without them `p32rewrite` parses with `'cstddef' file not found` (stderr is discarded) and the p32 tree comes out wrong: 4 TUs fail later** |
| GCC 14 libstdc++ | `libstdc++-14-dev` |
| NDK r30 (`get-ndk.sh`), SDK build-tools 35.0.0, android-35 | copied from Docker Hub `cimg/android:2026.10.1-ndk` (NDK 30.0.16248370) |
| meson 1.7.0 | PyPI |
| arm64 system image | none (qemu tests only, not needed) |

## Phase 1 (reproduce the official build): done

`bo1-zombies-debug.apk` from unchanged `main` 3401a66: 9,782,730 bytes, sha256
`c688d91e395cf8f2c28f93922e5079004a8b52f370d558162b53a171a9d1eab0`, `org.bo1port.zombies` 0.3, libs `libbo1.so`
(17.9 MB stripped), `libmain.so`, `libSDL2.so`, `libdxvk_d3d9.so`. Engine 1247 TUs, 0 errors. Not published: it's
the official app rebuilt.

Problems on the way, all environmental:
- p32 tree without rewrites: `p32rewrite` couldn't load `libclang-cpp.so.21.1`. Fixed (symlinks above).
- p32 tree with partial rewrites (glass_renderer, db_registry, ui_shared_obj, d3d9_resources failed:
  `BO1_P32` mismatches, `addrspacecast` in static initializers): libc++ headers not found inside libtooling. Fixed
  (symlinks above).
- `build-dxvk.sh linux`: `ninja ... src/d3d9/libdxvk_d3d9.so` has no such target on Linux (meson names it
  `libdxvk_d3d9.so.0.20601`). Desktop test path only. Skipped.

## Phase 2 (Turnip): built

- First DXVK build of the patch failed: `wsi_window_sdl2.cpp: use of undeclared identifier 'GetProcAddress'`. Fixed by
  including `util_win32_compat.h` (f89909f).
- Checked in the APK: `libdxvk_d3d9.so` holds `BO1_VK_GET_INSTANCE_PROC_ADDR`, `SDL_GetWindowWMInfo` and
  `vkCreateAndroidSurfaceKHR`; `libmain.so` holds the `vulkan driver:` messages; `libadrenotools.so` exports
  `adrenotools_open_libvulkan`; the hook libraries need only system libraries (static libc++).
- Not verified: anything at run time. There's no Android device or emulator here.

## Known risks

- **Untested on hardware.** The likeliest failure points, each logged:
  - `adrenotools_open_libvulkan` returning NULL. Details in logcat, tag AdrenoTools: namespace or hook libraries; the hook
    dir is the extracted native lib dir, and `extractNativeLibs="true"` was already set.
  - The custom loader creating no device.
  - DXVK rejecting the Turnip device (needs Vulkan 1.3 and its features). The engine then falls back to OpenGL ES as
    today.
- If the rendering bug is in DXVK or the engine and not in the Qualcomm driver, Turnip won't fix it. The baseline vs
  Turnip comparison in the test answers that.
- The driver is probed before the engine is loaded below 4 GB. The probe's allocations are 64-bit mmaps (top-down),
  but a driver that maps low memory could shrink the engine's heap range. Look for a boot failure in `bo1.log`
  ("could not be loaded below 4 GB").
- The driver library is copied to internal storage on every start (a few MB, a few ms).
- The meta.json parsing is minimal: flat string fields `name`, `driverVersion`, `libraryName`.
- Debug signature, `versionCode` 3 like the official app. A different signing key is fine, since it's another package.
