# CLAUDE.md - BO1 Port · Turnip (Android port of the decompiled engine)

An Android (arm64) port of the decompiled Call of Duty: Black Ops 1 engine (Treyarch IW-family, Quake 3 lineage) that
runs Zombies and Multiplayer. Fork of Zephelinn's Android port (README.md, Credits), adding a Vulkan driver of the
player's choice (Mesa Turnip through libadrenotools) and second-screen controls for dual-screen handhelds (AYN Thor).
The engine is an **MP-engine decompile with the SP zombies game module ported in** (`BO1_MP` defined;
`+set bo1_zombies 1` switches it to zombies), about 1.38 M lines in `src/`. The player supplies the original game's
files (about 9.4 GB for Zombies, copied to the device); the repo holds no game assets and must never hold any.

Workstreams. Each has its own doc; read it before working in that area:

| workstream | doc | code |
| --- | --- | --- |
| Android port: build, loader stub, settings, touch controls, menus | `docs/android-port.md`, `docs/INSTALL-ANDROID.md` | `src/native/`, `android/`, `cmake/native.cmake`, `tools/native/`, `tools/p32/` |
| Vulkan driver choice (Turnip through libadrenotools) | `STATUS.md` | `src/native/android/bo1_driver.c`, `tools/native/build-dxvk.sh` (`BO1_LOADER`), `tools/native/build-adrenotools.sh` |
| Second-screen controls (look pad, size press, remapping) | `docs/second-screen.md` | `android/java/org/bo1port/zombies/SecondScreen*.java`, `src/native/native_second_screen.cpp` |
| D3D9 → OpenGL ES 3 shim (MojoShader), Vulkan through DXVK | `src/web/d3d9/README.md`, `docs/android-port.md` (Renderers) | `src/web/d3d9/`, `src/native/vk/`, `third_party/mojoshader/` |
| Co-op (4 players, up to 8), join by address, local network | `docs/multiplayer.md` (the coop mod: Zephelinn's repo) | `src/client_mp`, `src/server_mp`, `mods/coop/` |
| Controllers | `docs/controllers.md` | `src/client/gpad_*`, `src/native/native_gamepad.cpp` |

`src/web/` is not about browsers any more: it is the platform layer the Android build compiles (the Win32 emulation
in `src/web/compat`, the D3D9 shim, input, files, network). The browser version itself (its page, server, hosting,
Emscripten build and docs) was removed from this fork; the upstream repositories keep it.

## Rules that are not negotiable

- **No game assets in the repo or in the app.** No `.ff`, `.iwd`, `.bik`, dumped retail scripts or textures. The
  player copies their own install to the device. Disc images or installers in the working tree (gitignored) are not a
  source for anything we ship.
- **Keep the decompiled engine complete.** The Windows build was removed, but the engine code still carries its
  MSVC-era constructs: leave them. Guard platform code with `#ifdef BO1_NATIVE` / `BO1_WEB` / `__ANDROID__`. Never
  reformat decompiled code; edit the smallest region.
- **Tag non-original engine code**: `// mod: ...` or `// mod (<topic>): ...` (`// mod (native):`, `// mod (coop):`,
  `// mod (gpad):`, `// mod (touch):`, `// mod (turnip):`); platform code `// web: ...`. Existing tags: `// zombies:`
  (SP-port work), `SP 0x00XXXXXX` (address of the original function in the retail SP exe), `retail` (the original
  game's behavior), `measured` (a number backed by a test run), `TEST SWITCH` (harness-only dvar or file), `NOT PORTED`
  (known gap), `BO1TODO`, `// XREF:` and `// [esp+..]` (raw IDA output). "KB" in comments = the MP decompile base this
  project started from.
- **Never `Read` a huge file whole.** `grep -n` first, then read a window. Largest: `game_mp/g_scr_main_mp.cpp` (18k
  lines, script builtins), `universal/com_expressions_eval.cpp` (14k), `gfx_d3d/r_image_wavelet.cpp` (12.5k),
  `ui/ui_shared.cpp` (11k), `game/g_scr_vehicle.cpp`, `cgame/cg_scr_main.cpp`, `database/db_load.cpp`.
- No commits/pushes unless the user asks. Keep `NOTICE.md`, `THIRD_PARTY_NOTICES.md`, every licence file, the credits
  and the "bring your own game files" / "not affiliated" wording.

## Repository map

| path | what |
| --- | --- |
| `android/` | `AndroidManifest.xml` (package `org.bo1port.zombies.turnip`), the Java side: `Bo1Activity` (SDL's activity), `SecondScreen*` (second-screen controls); `android/test/`: off-device tests (`tools/native/test-second-screen.sh`) |
| `src/native/` | the Android / Linux platform code (not rewritten by tools/p32): SDL2 window and video (`native_video.cpp`), controllers (`native_gamepad.cpp`), touch controls (`native_touch.cpp`), second screen (`native_second_screen.cpp`), audio, low memory, extras; `android/bo1_boot.c` (libmain.so: loads libbo1.so below 4 GB) and `android/bo1_driver.c` (the Vulkan driver choice); `vk/` (the DXVK bridge) |
| `src/web/` | the platform layer shared with the native builds: `compat/` (Win32 emulation over pthreads/POSIX), `d3d9/` (D3D9 shim on OpenGL ES 3), `web_input.cpp` (input queue), `web_main.cpp`, `web_net.cpp`, `web_stats.cpp`, ... |
| `src/win32/` | what is left of the Windows platform layer: the files the native build shares (`cmake/native.cmake` `BO1_NATIVE_SHARED_WIN32`) and the headers the engine includes |
| `src/qcommon/` | `Com_Init`/`Com_Frame` (`common.cpp`), threads + Win32 events (`threads.cpp`), netchan (`net_chan_mp.cpp`), msg/huffman, cmd/cbuf, collision (`cm_*`), mod engine code (`cm_mapkit`, `cm_noperks`) |
| `src/universal/` | dvars, filesystem + search paths (`com_files.cpp`), memory (`physicalmemory.cpp`, `com_memory.cpp`), `q_shared`, asserts (`assertive.h`), expressions |
| `src/database/` | fastfile (zone) loading: `db_registry.cpp` (zone lists, pools, `DB_Thread`), `db_file_load.cpp` (overlapped reads, inflate), `db_load.cpp` (asset deserializers) |
| `src/clientscript/` | GSC/CSC script compiler + VM (`cscr_*`); `Scr_ReadFile` (mods override fastfile scripts), generated `maps/_modstack_list.gsc` |
| `src/server_mp/`, `src/client_mp/` | MP netcode used by zombies: connect handshake, snapshots, usercmds (NB `sv_client_mp.cpp` lives in `client_mp/`); Android additions: `cl_touch.cpp`, `cl_extras.cpp` (Settings > EXTRAS), `cl_play.cpp` (Play screen), `cl_joinmenu.cpp` |
| `src/server/`, `src/client/` | shared/SP-side pieces; `client/cl_keys.cpp` (binds, keycodes), `client/cl_gamepad.cpp`, `client/gpad_*` |
| `src/game/`, `src/game_mp/`, `src/game_sp/` | game module: actors/AI (`game/actor*`), MP game + script builtins (`game_mp/g_scr_main_mp.cpp`), SP zombies port (`game_sp/`, `G_SP_*`, `scr_sp_tables.h` layered builtin tables, headless test labs `g_sp_headless_*`, `bo1_testclient*`) |
| `src/cgame/`, `src/cgame_mp/` | client game: HUD (`cgame/cg_sp_hud.cpp`, zombies scorebars), view, prediction, entities |
| `src/bgame/` | shared game code: pmove, weapons defs, anim, misc tables (`bg_misc.cpp` reads `playlists_sp.info`) |
| `src/gfx_d3d/` | D3D9 renderer: front end `r_*`, back end `rb_*` (render thread), `r_state.cpp` (all state + the one draw call), `r_shade.cpp` (constants), `r_material*.cpp`, `r_init.cpp` (window/device) |
| `src/sound/` | sound engine (portable C); on Android the native mixer (`src/native/native_audio.cpp`, FFmpeg for xWMA) |
| `src/ui/`, `src/ui_mp/` | menu system (menus come from fastfiles; `ui_shared.cpp` key handling), playlists |
| `src/physics/`, `tl/` | rigid bodies (GJK), TL job queue (`tl/jobqueue`), TL physics defs |
| `src/xanim/`, `src/EffectsCore/`, `src/DynEntity/`, `src/glass/`, `src/flame/`, `src/ik/`, `src/ragdoll/`, `src/vehicle/`, `src/turret/`, `src/aim_assist/` | animation, FX, dynamic entities, and the rest of the gameplay systems |
| `src/live/`, `src/DW/`, `DemonWare/`, `src/steam/` | online services: mostly dead code (`BO1_LIVE`/`BO1_DW` never defined), still in the build (`cmake_files.cmake`) |
| `src/libs/`, `src/zlib/`, `src/jpeg/`, `src/minilzo/`, `src/speex/`, `third_party/` | third-party (licences: `THIRD_PARTY_NOTICES.md`); `third_party/` holds MojoShader and sse2neon |
| `tools/native/` | the Android / Linux build scripts (`build-*.sh`, `android-setup.sh`, `get-ndk.sh`, `docker/Dockerfile`) and Linux test harnesses (`coop-test.sh`, `lan-test.sh`, `fx-test.sh`, `run-arm64.sh`, the emulator scripts), `test-second-screen.sh` |
| `tools/p32/` | the 32-bit-pointer rewrite (`p32rewrite.cpp`, `p32tree.py`) and the clang 21 patches the build needs |
| `docs/` | `android-port.md`, `INSTALL-ANDROID.md`, `second-screen.md`, `controllers.md`, `multiplayer.md` |
| `CMakeLists.txt`, `cmake_files.cmake`, `cmake/native.cmake` | the build: `cmake_files.cmake` is the single list of every engine source file (it creates the target); `CMakeLists.txt` requires `-DBO1_NATIVE=ON` and includes `cmake/native.cmake` |
| `STATUS.md`, `README.md`, `.github/` | release state and device tests; the project page; issue templates |

## How the engine runs (anchors; line numbers drift, grep the function)

- **Boot**: `SDL_main` (`src/native/android/bo1_boot.c`) loads libbo1.so below 4 GB and calls `bo1_android_main`
  (`src/web/web_main.cpp`, the retail `WinMain`'s steps) → `Sys_InitializeCriticalSections`, `PMem_Init`, `Dvar_Init`, command line
  into `sys_cmdline` (1024 bytes) → `Com_Init` (`qcommon/common.cpp`, setjmp error trap) → `Com_ParseCommandLine` splits on
  `+`; `set` lines apply first (`Com_StartupVariable`), FS, configs, `NET_Init`, `SV_Init`, `CL_Init`, `R_InitThreads`,
  `SND_Init`; other `+commands` run after (`Com_AddStartupCommands`), then `Com_LoadFrontEnd` (the zombies main menu is a
  *level*, `map frontend`, with 3D TV menus).
- **Frame**: `Com_Frame` → `Cbuf_Execute`, `SV_Frame`, `CL_Frame`, `SCR_UpdateScreen`. Errors longjmp back (`Com_Error`).
- **Threads** (`qcommon/threads.h` contexts): main, render back end (`RB_RenderThread`), TL job workers, server
  (`SV_ServerThread`, reads the socket when a server runs), sound occlusion, database (`DB_Thread`), stream (texture +
  sound streaming). Sync via `Interlocked*`, critical sections, Win32 events. Thread-affine asserts in `com_files.cpp`.
- **Server/client**: zombies is a **listen server** in the host process (local client over loopback). Netchan packets
  ≤ 1264 bytes, huffman above netchan. Transport seam: `Sys_SendPacket` / `Sys_GetPacket` (`src/web/web_net.cpp`, a UDP socket).
  `NET_SendPacket` (`net_chan_mp.cpp`) routes loopback in memory. 32 clients supported engine-side.
- **Filesystem** (`universal/com_files.cpp` `FS_Startup`): later search paths win. Order: dev folders, `players`, `main`,
  `fs_game` (must be `mods/<x>`), then each `fs_mods` entry and `mods/_stack`. IWDs = zips (`main/iw_*.iwd`,
  `localized_*`). `fileSysCheck.cfg` missing is fatal. **Fastfiles** resolve from the *exe folder*
  (`zone/Common/<z>.ff`, `zone/<Language>/<z>.ff`), not `fs_b`; videos from the cwd. Three roots, backslash paths.
- **Zones for zombies**: boot `code_pre_gfx_mp`; then `code_post_gfx_mp`, `code_post_gfx`, `patch_mp`, `patch`, `ui_mp`,
  `frontend`; per map `common_zombie(_patch)`, `<map>(_patch)`, `en_*` localized copies. Plain zlib, no signing (`IWffu100`).
- **Scripts**: `Scr_ReadFile` (`clientscript/cscr_parser.cpp`) takes a loose file from the search path when a mod is active,
  else the fastfile copy. Builtins: MP tables in `game_mp/g_scr_main_mp.cpp`, SP layered tables (`game_sp/scr_sp_tables.h`);
  project-added builtins `bo1_mapkit_*`, `bo1_mod_*` (`game_sp/g_scr_sp_ai.cpp`).
- **Renderer**: front end builds sorted draw surfaces → command buffer → back end thread (`rb_backend.cpp`). Shaders are
  precompiled SM3 bytecode inside fastfiles. `r_smp_backend 0` runs the back end inline.
- **Sound**: the engine's sound code over the `SD_*` driver API; on Android `src/native/native_audio.cpp` mixes (SDL audio, FFmpeg for xWMA). Banks are fastfile assets; streamed audio through the FS layer.
- **Input**: `MainWndProc` → `Sys_QueEvent` → `CL_KeyEvent`; mouse via `IN_MouseMove` → `CL_MouseEvent`; gamepad →
  `CL_GamepadEvent` / `CL_GamepadButtonEventForPort`; usercmd in `client_mp/cl_input_mp.cpp` `CL_CreateCmd`.
- **Memory**: PMem 328 MiB committed at start (+256 MiB with `bo1_mod_zones`), hunk reserve/commit. ~400 MB total.

## Dvars and conventions you will meet

- `bo1_zombies 1` (command line only) selects zombies. ~172 `bo1_*` dvars are project additions: `bo1_mod_*` (mod features,
  several must be on the command line because they are read at map load: `bo1_mod_maxactors`, `bo1_mod_zones`),
  `bo1_headless*`, `bo1_testclient*`, `bo1_autoquit`, `bo1_shots`. Find any dvar with `grep -rn '"<name>"' src`.
- Engine prefixes: `Com_ Sys_ R_ RB_ CG_ G_ SV_ CL_ Scr_ DB_ BG_ CM_ FS_ SND_ SD_ UI_ GPad_`. Added: `G_Mapkit_*`,
  `CM_NoPerks_*`, `G_SP_Headless*`, `g_mod*`.
- C++: 4 spaces (`.editorconfig`), LF. GSC: tabs. Mod GSC files start `// <NAME> MOD - not part of the original game.`
- Doc style (READMEs, MAPKIT.md, docs/): plain declarative sentences, no promotion; every dvar with default and range;
  "retail" for the original; numbers with their conditions; exact log lines in backticks; sections like
  Start it / Leave it / How it works / Not done / Limits. Titles of mod docs end "(NOT part of the original game)".

## Build, run, test (Android)

The build runs on Linux x86-64 (the `bo1-x64` container of `tools/native/docker/Dockerfile`: Debian 13, LLVM 21), with
the repo at `/bo1/src`, NDK r30 at `/bo1/ndk/ndk-r30/android-ndk-r30` and work folders under `/bo1`. Run the scripts
with `bash` (several are mode 644):

```
bash tools/native/get-ndk.sh; bash tools/native/build-llvm.sh       # once: NDK, patched clang 21.1.8
bash tools/native/android-setup.sh; bash tools/native/build-ffmpeg.sh arm64-v8a
mkdir -p /bo1/src/build && bash tools/native/build-p32.sh   # regenerates /bo1/p32tree (its x64 half is optional)
bash tools/native/build-android.sh                       # libbo1.so, libmain.so, SDL2
bash tools/native/build-dxvk.sh arm64-v8a; bash tools/native/build-adrenotools.sh
bash tools/native/build-apk.sh                           # /bo1/android/out/bo1-zombies-turnip-debug.apk
bash tools/native/test-second-screen.sh                  # off-device tests of the second screen's maths
```

`STATUS.md` has the exact steps and the container stand-ins of the last build. The p32 rewrite reads the x64 compile
database: if `p32rewrite` cannot find a header (its stderr is discarded) the tree comes out half rewritten and the
Android build fails later with layout `static_assert`s.

Run and test on a device: install the APK over the existing one (`adb install -r`; never uninstall: Android deletes the
game folder with the app), then read `/sdcard/Android/data/org.bo1port.zombies.turnip/files/bo1.log` (stdout and
stderr of the engine: `vulkan driver: ...`, `second screen: ...`, `native: ...`). `files/args.txt` holds extra engine
arguments; `files/cmd.txt` (TEST SWITCH) feeds console commands to the running game. Linux can run the engine
without a device to check engine changes: the AArch64 build under qemu-user (`tools/native/run-arm64.sh`), co-op and
LAN games on one machine (`coop-test.sh`, `lan-test.sh`), effects and sounds (`fx-test.sh`). You
cannot run the game here: state exactly what was verified and give the user a device test plan (adb commands and the
log lines to look for) for everything else.

Check engine edits with the Android build itself (`build-android.sh` prints the failed TUs and errors by kind and
file). D3D9 shim unit tests: `src/web/d3d9/test/`.

## Mods

The script mods (horde, nightmare, mapkit, noperks, sandbox, zinfo, coop, `_stack`) are not in this repo:
they live in Zephelinn's repo (https://github.com/Zephelinn/BO1-Ascension-ARM64/tree/main/mods), with the base-game file
`data/main/playlists_sp.info`. On Android a mod folder goes to the device's `files/game/mods/<name>/` and is chosen in
Settings > EXTRAS > MOD. Loaded only with `+set fs_game mods/<x>` (one mod; also moves logs/config) or `+set fs_mods "a b"` (stacked; `mods/_stack`
is appended automatically and must not be named). `_stack` ships the merged retail hook scripts
(`maps/_zombiemode_ffotd.gsc`, `maps/zombie_pentagon_ffotd.gsc`) that call `maps\_modstack::run(hook)`; each mod registers
hooks in `maps/<mod>/_hooks.gsc` `register()` with `maps\_modstack::add(hookName, func)`. Hook names:
`_zombiemode_ffotd::main_start|main_end`, `zombie_pentagon_ffotd::main_start|main_end`, `mod_zones/<zone>::main`.
Mods: `horde` (rounds chain, big crowds, `bo1_mod_maxactors`), `nightmare` (special enemies from other maps via
`bo1_mod_zones`), `mapkit` (JSON map layouts on Five's assets, `MAPKIT.md` in the mod), `noperks`, `sandbox` (Five test
pads), `zinfo` (Tab zombie counter), `coop` (lobby characters / 5-8 players), `_stack` (base). The retail map scripts are
not in the repo; `bo1_dumpscript <name>` writes one from the loaded fastfiles to `<fs_h>/dump/` for local reading (never commit dumps).


## Multiplayer and controllers (summaries; details in the docs)

- **Multiplayer model**: star topology; the host's process is the listen server. On Android: the Play screen starts a
  solo, hosted or bot game; others join by address or from the local-network list (`docs/android-port.md`). Characters:
  retail scripts pick the character by entity number, so the first player to pick character C gets client number C
  (`bo1_slot`), duplicates get 4..7 and `mods/coop` maps them via `bo1_lobby_chars`. Host dvars `sv_maxclients`,
  `bo1_expected_players`, `systemlink 1`.
- **Controllers**: the console gamepad stack (aim assist, analog usercmds) is compiled in; the platform-neutral backend
  `src/client/gpad_backend.h` (`GPad_Backend_Poll`) is fed on Android by `src/native/native_gamepad.cpp` (SDL), which
  also merges the touch controls and the second screen's pad into slot 0.


## Finding things fast

- Where is X registered/used: `grep -rn '"x"' src` (dvars, commands, script builtin names are string literals).
- SP-port vs original: `grep -rn "// zombies:" <dir>`; project additions: `grep -rn "// mod" <dir>`.
- Retail address of a function: comments `SP 0x...` (SP exe) next to the definition.
- Which files a feature spans: `grep -rln <Symbol> src | xargs wc -l | sort -n`.
- Build membership: `cmake_files.cmake` (`grep -n "<file>" cmake_files.cmake`); the Android build takes that list minus the exclusions in `cmake/native.cmake`.
- Debug output on the engine side goes to `Com_Printf` → `console_mp.log` and, on Android, stdout → `files/bo1.log`; script side `LogPrint` → `games_mp.log`.


## Using subagents (parallel work)

The tree is too large for one context. Fan out, keep conclusions, and give each agent exclusive files.

- **Exploration**: one `Explore` agent per subsystem, all in one message ("very thorough", ask for path:line facts, a
  word budget, and a recommendation). Good splits: platform/build · renderer · netcode/players · input/UI ·
  sound/database/assets · scripts/mods/tools · Android Java side.
- **Implementation**: `general-purpose` agents with **disjoint file ownership** stated in the prompt, plus: "other
  agents edit concurrently: re-read a file right before editing it, keep edits minimal, never revert or reformat code
  you did not write, no git state changes".
- **Gates every agent must pass before reporting**: the Android build (`build-android.sh`: 0 failed TUs) for engine C/C++; `tools/native/test-second-screen.sh` for the second screen's Java maths.
  Ask for a short final report: files changed, verified vs not, open decisions.
- **Prompt preamble to reuse**: repo path, "decompiled IDA output; tag added code `// mod (<topic>):`; keep the
  decompiled engine complete; cannot run the game on a device here; read <doc> first; own only <paths>".

## Known stale references

- Removed from this fork, still mentioned in `src/` comments and in `docs/multiplayer.md`: the browser version
  (`web/`, `.railway/`, `cmake/web.cmake`, `CMakePresets.json`, `docs/web-port.md`, `docs/web-engine-interface.md`),
  the Windows launchers and harness (`Play.bat`, `setup.ps1`, `tools/headless.ps1`, `tools/coop*.bat` / `coop.ps1`,
  `tools/mod-launcher.hta`), `docs/building-windows.md`, and the developer tools `tools/mapkit/`,
  `tools/syntax-check*`, `tools/*_flags.js`, `mods/`, `data/`, and the Windows build with its Windows-only engine code
  (most of `src/win32/`, `src/tracy/`, `src/groupvoice/`, `src/CubeMapGenLib/` and `src/mjpeg/` sources, `src/vpx/`,
  `src/monkey/` sources, `src/libs/libvpx-1.5.0/`, the XAudio2 sound driver; their licence files stay).
  `src/universal/profile.h` still includes Tracy under `TRACY_ENABLE`, which nothing defines now. The upstream
  repositories (README.md, Credits) still have them; the
  `archive/pre-cleanup` branch has the state before the cleanup.
- Never in the repo: `mods/_stack/README.md`, `notes/*.md` (run/lab IDs like `L60`, `mapkit-fix-3c`),
  `tools/{mergecheck.sh, realtest.ps1, stress.ps1, unjam_table.mjs, gen_sp_builtins.py}`, `mods/mapkit_horde`.
  `cm_noperks.cpp` points at `mods/zinfo` for the noperks script (it is `mods/noperks`).
