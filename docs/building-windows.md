# Building the Windows version (NOT part of the original game)

The Windows build (MSVC, x86, Direct3D 9) links three proprietary SDKs that their owners do not allow to be
redistributed, so this repository does not contain them. The Android, Linux and browser builds do not need any of
them; see `docs/android-port.md` and `docs/web-port.md`.

| files | where they go | where to get them |
| --- | --- | --- |
| Steamworks SDK headers | `src/steam/*.h` (the contents of the SDK's `public/steam/`) | https://partner.steamgames.com/downloads/list (free Steam account). The engine was written against the SDK whose interface versions include `SteamClient023`, `SteamUser023`, `SteamFriends018` and `STEAMTIMELINE_INTERFACE_V004` (1.62 era). |
| `steam_api.lib`, `steam_api.dll` | `src/steam/redistributable_bin/` | the same SDK: `redistributable_bin/` |
| Bink SDK sources: `bink.h`, `radbase.h`, `rad3d.h`, `binktextures.h`, `binktextures.cpp`, `dx9rad3d.cpp` | `src/binklib/` | the Bink SDK (Epic Games Tools, licensed); the 2008-era Windows SDK with its Direct3D 9 example code. The Android, Linux and browser builds use `src/web/compat/binklib/` instead |
| `binkw32.dll` | `src/binklib/` and next to the built exe | your Black Ops install folder (the game ships it) |
| `binkw32.lib` | `src/binklib/` | an import library made from that DLL: `dumpbin /exports binkw32.dll`, write the names to `binkw32.def` (`LIBRARY binkw32` + `EXPORTS` + one name per line, keeping the `_Name@N` decoration), then `lib /def:binkw32.def /machine:x86 /out:binkw32.lib` in a Visual Studio x86 developer prompt |
| `nvapi.h`, `nvapi.lib` | `src/nvapi/` | NVIDIA's NVAPI SDK (https://github.com/NVIDIA/nvapi), x86 library (`x86/nvapi.lib`) |

`.gitignore` keeps these files out of commits.

Then build as in `CLAUDE.md`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DDXSDK_DIR=C:/path/to/DXSDK_June2010
cmake --build build --config Release
powershell -NoProfile -ExecutionPolicy Bypass -File setup.ps1
Play.bat
```

Not verified: a Windows build from a fresh clone with the SDKs gathered this way. The upstream project this repository
forks shipped the files in the repository; this project's own work was built and tested on Linux and Android.
