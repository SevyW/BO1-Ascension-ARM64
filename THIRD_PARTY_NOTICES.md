# Third-party software

The code of this repository and the Android app includes or links the components below. Each keeps its own licence;
the full texts are in the component's folder, and `tools/native/build-apk.sh` puts them in the app under
`assets/licenses/`.

The engine itself is decompiled from *Call of Duty: Black Ops* (Activision). It is not offered under an open-source
licence; see `NOTICE.md` and `README.md`. The code this project added (Android, Linux and web platform layers,
renderer bridge, tools, mods, web site, documentation) is the maintainers' own work.

## In the Android app and the Linux builds

| component | version | licence | source |
| --- | --- | --- | --- |
| DXVK (Vulkan renderer for D3D9), with this project's patches | v2.6.1 | zlib/libpng | https://github.com/doitsujin/dxvk (cloned by `tools/native/build-dxvk.sh`) |
| SDL2 (window, input, sound, Android activity) | 2.32.x | zlib | https://github.com/libsdl-org/SDL (fetched by `tools/native/android-setup.sh`) |
| FFmpeg libavcodec + libavutil, WMA 2 decoder only, static | n7.1 | LGPL 2.1 or later (no GPL or nonfree parts enabled) | https://github.com/FFmpeg/FFmpeg (built by `tools/native/build-ffmpeg.sh`) |
| MojoShader (D3D9 shaders to GLSL ES), vendored | see `third_party/mojoshader/VENDOR.md` | zlib | `third_party/mojoshader/LICENSE.txt` |
| sse2neon (SSE intrinsics on ARM NEON) | see `third_party/sse2neon/` | MIT | `third_party/sse2neon/LICENSE` |
| dlmalloc (the engine's low heap on 64-bit) | 2.8.6 | public domain (CC0) | `third_party/dlmalloc/` |
| zlib | 1.2.3 | zlib | `src/zlib/` |
| Independent JPEG Group's libjpeg | 6b | IJG licence | `src/jpeg/README` ("This software is based in part on the work of the Independent JPEG Group.") |
| miniLZO | 2.09 | GPL 2.0 or later | `src/minilzo/` (part of the retail engine) |
| LibTomCrypt | 1.17 | public domain / WTFPL | `src/libs/libtomcrypt-1.17/` |
| LibTomMath | 1.0 | public domain / Unlicense (dual) | `src/libs/libtommath-1.0/LICENSE` |
| Speex | see `src/speex/readme.txt` | BSD-style (Xiph.Org) | `src/speex/` |

## Windows build only (not in this repository)

The Windows (MSVC, Direct3D 9) build links proprietary SDKs that may not be redistributed. They are not in this
repository; the upstream repository's [docs/building-windows.md](https://github.com/Zephelinn/BO1-Ascension-ARM64/blob/main/docs/building-windows.md) says where each comes from and where it goes
(this fork builds for Android only).

| component | owner | needed for |
| --- | --- | --- |
| Bink video (`binkw32.dll`, `binkw32.lib`) | Epic Games Tools (RAD Game Tools) | cinematics |
| Steamworks SDK (`src/steam/`: headers, `steam_api.lib`/`.dll`) | Valve | Steam sign-in, P2P, achievements |
| NVAPI (`nvapi.lib`) | NVIDIA | driver queries |

The native and web builds use the project's own declarations of the few Steam functions the engine calls
(`src/web/compat/steam/`, Steam reported as not running) and no Bink or NVAPI.

## Windows build only (in this repository)

| component | version | licence | source |
| --- | --- | --- | --- |
| libvpx (video capture) | 1.5.0 | BSD 3-clause | `src/libs/libvpx-1.5.0/` |
| Tracy profiler client | see `src/tracy/` | BSD 3-clause | `src/tracy/` |
