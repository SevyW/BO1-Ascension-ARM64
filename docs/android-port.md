# Native builds: Linux (x86, x86-64, AArch64) and Android (arm64-v8a)

The native build is the web build (the browser version, which stays in the upstream repositories) without Emscripten: the same engine sources, the same Win32
emulation (`src/web/compat`) and the same D3D9 shim on OpenGL ES 3 (`src/web/d3d9`), with `src/native/` (SDL2: window,
GL context, controller) in place of the browser glue. `cmake -DBO1_NATIVE=ON`, see `cmake/native.cmake`.

| Target | Pointers | State |
| --- | --- | --- |
| Linux x86 (`BO1_NATIVE_ARCH=x86`) | 32-bit, as the game was written | renders, plays, controller |
| Linux x86-64 (`x64`) | 32-bit pointer objects in a 64-bit process | renders, plays repeated rounds |
| Linux AArch64 (`arm64`) | same | renders and plays under qemu-user |
| Android arm64-v8a (`arm64` + `BO1_NATIVE_ANDROID`) | same | dedicated server runs on bionic under qemu-user; the app is built but has not run on a device |

## 64-bit: 32-bit pointer objects

The engine assumes 4-byte pointers everywhere (struct layouts checked with `static_assert`, fastfile assets loaded
in place, pointers kept in `int`s). The 64-bit builds keep that true instead of fixing every place:

* `tools/p32` (a Clang LibTooling rewriter plus `p32tree.py`) generates a copy of the tree in which every pointer
  *object* is `__ptr32 __uptr` (4 bytes, zero-extended on load). Values in flight (parameters, returns, casts) stay
  native. It also turns `long` into `int`, `long double` into `double`, and wraps what Clang/LLVM cannot do with
  32-bit pointers in macros from `src/native/native_p32.h` (indirect calls, static initializers, `delete`, pointer
  arguments passed through `...`, signed integers cast to pointers, unsigned subscripts).
* Everything the engine can point at is below 4 GB: `src/native/native_lowmem.c` (heap, thread stacks; on Linux the
  executable is linked non-PIE).
* Pointers that come from the system (static data of libc, GL strings, `FILE`) stay in native pointers:
  `src/web/compat/bo1_hostptr.h`, `bo1_stdio.h`.
* Clang 21 needs two fixes, in `tools/p32/*.patch` (x86-64: by-value structs that start with a `__ptr32` member;
  AArch64: 8/16-bit stores through a `__ptr32` address wrote 32 bits). The NDK's clang has the same code, so the
  Android build uses the patched compiler with the NDK's sysroot and runtime libraries.

## Android

* `libbo1.so` is the engine, position independent. `libmain.so` (`src/native/android/bo1_boot.c`) is what SDL's
  `SDLActivity` runs: it finds the largest free range below 4 GB, loads `libbo1.so` into it with
  `android_dlopen_ext(ANDROID_DLEXT_RESERVED_ADDRESS)` and gives the rest to the engine's heap.
* Inside `libbo1.so`, `malloc` is the low heap (`-Bsymbolic`, libc++ and the compiler runtime static). bionic, SDL
  and the GL driver keep their own allocator, whose blocks are above 4 GB.
* The window is the display; the shim scales the engine's back buffer into it (`d3d9shim_set_present_size`).
* Input is the game controller (SDL) or the touch controls ("Touch controls" below).
* Sound: `src/native/native_audio.cpp`, a software mixer on SDL audio (dry mix only: no reverb or EQ; xWMA through
  FFmpeg).
* Memory: Kino der Toten touches about 1.5 GB of the low heap in game. The stub logs the free ranges it found below
  4 GB at the top of `bo1.log`.

### Build (on the build host, inside the `bo1-x64` container)

The container: `tools/native/docker/Dockerfile` (Debian 13, LLVM 21 from apt.llvm.org), the work folder mounted at
`/bo1`, this repository at `/bo1/src`. `tools/native/build-all-android.sh` runs every step below from a fresh
container: `get-ndk.sh` (NDK r30), `build-llvm.sh` (clang 21.1.8 with the patches of `tools/p32`), then the list.
Not verified: a run from a fresh container (the maintainers' container grew step by step).

```
tools/native/android-setup.sh    # once: JDK, Android SDK build tools + platform, SDL2 source, arm64 system image
tools/native/build-p32.sh        # generate the 32-bit-pointer tree (also builds the x86-64 Linux binary)
tools/native/build-android.sh    # SDL2 for Android, libbo1.so, libmain.so, bo1_boot_test
tools/native/build-apk.sh        # bo1-zombies-debug.apk (javac + d8 + aapt2 + apksigner, no Gradle)
```

`tools/native/android-stubs.sh` + `run-android.sh` run the engine library as a dedicated server under `qemu-aarch64`
with the linker and libc of the Android system image, which is how the loader and the allocator were tested.

### Install

`docs/INSTALL-ANDROID.md` is the guide for players. In short:

1. `adb install bo1-zombies-debug.apk` (or copy the file to the device and open it).
2. Start the app once: it creates `Android/data/org.bo1port.zombies/files/` and says the game files are missing.
3. Copy the contents of the PC install (`Call of Duty Black Ops`: `localization.txt`, `zone/`, `main/`, ...) into
   `Android/data/org.bo1port.zombies/files/game/`. With adb:
   `adb push "<install>/." /sdcard/Android/data/org.bo1port.zombies/files/game/`
4. Optional: `files/args.txt` with one line of engine arguments, e.g. `+set bo1_zombies 1 +devmap zombie_theater`
   to start a map directly. Without the file the app starts the game's own front end (`+set bo1_zombies 1`).
5. Connect a controller and start the app. `files/bo1.log` is the engine's output; that file is what to send back
   when something goes wrong.

## Playing with others: dedicated server, join by address

The native builds have a real UDP socket (`src/web/web_net.cpp`, `BO1_NATIVE`); the browser build keeps its rings.
Builds of this engine on any platform reach each other by address. Retail Black Ops cannot join: the stats buffer
and the co-op additions differ.

Zombies server (Linux, no window):

```
bo1 +set fs_b <game> +set fs_h <home> +set dedicated 1 +set bo1_zombies 1 +set systemlink 1 +set net_port 28960
    +set sv_maxclients 4 +set bo1_expected_players 1 +set bo1_lobby_chars "0 1 2 3" +set sv_maxRate 25000 +map zombie_theater
```

`bo1_expected_players` is how many players the first round waits for (default timeout `bo1_expected_timeout` 60 s).
After Game Over the server starts the same map again (`coop: zombies server: starting <map> again`), unless
`sv_mapRotation` names zombie maps. Open UDP 28960 on the host.

Joining:

* The join screen (`src/client_mp/cl_joinmenu.cpp`): F4, or the controller's Back button while no game is being
  played, or the console command `joinmenu`. An address line, an on-screen keypad and the last four addresses
  (`bo1_join_recent`, archived, default ""). D-pad moves, A presses, X deletes, Start joins, B closes. Port 28960 is
  assumed when none is given.
* Or the command line / `args.txt`: `+set bo1_zombies 1 +set systemlink 1 +set name <n> +connect <ip>:28960`.

`tools/native/coop-test.sh` runs a dedicated server and one client on one machine.

Verified: an x86-64 Linux client on an x86-64 dedicated server, and on an AArch64 one run under qemu-user (rounds,
Game Over, the restart, the client following each time). Not verified: Android or Windows as either side.

Not done: join codes / a lobby (the browser build's lobby server was not used by the native builds and is not in this fork), LAN discovery,
more than one remote client in a test, the Android build as a host.

## Multiplayer with bots, Combat Training

`+devmap <mp map>` without `bo1_zombies` is the multiplayer game. Two ways to get bots, both read by the retail
script `maps/mp/gametypes/_bot.gsc`:

| mode | dvars |
| --- | --- |
| offline match (no rank) | `+set splitscreen_playerCount 1 +set splitscreen_botFriends <n> +set splitscreen_botEnemies <n> +set splitscreen_botDifficulty <easy/normal/hard/fu>`; gametype `tdm` or `dm` only |
| Combat Training (rank, unlocks, classes) | `+set onlinegame 1 +set xblive_basictraining 1 +set bot_friends <n> +set bot_enemies <n> +set bot_difficulty <...>` |

Both need `+set sv_maxclients 18` (or more than the players), `+set sv_botsAllowMovement 1 +set sv_botsPressAttackBtn 1`
(the engine registers them as 0) and a gametype, e.g. `+set g_gametype tdm +devmap mp_nuked`.

Profiles (`src/web/web_stats.cpp`): the server keeps each human player's stats in
`<fs_h>/players/profiles/<name>.ct.stats` (Combat Training) and `<name>.mp.stats`, 40548 bytes each, loaded on
joining and written at the end of a match and on leaving. Set the name with `+set name <n>`. Delete the files to
start over.

The in-game menus (team, class) follow the controller's d-pad and A.

Not done: the multiplayer front end as the way in (it is the PC mouse menu; a match is started from the command
line or `args.txt`), xWMA sounds, memory use on a phone (a match on mp_nuked with Mesa's software renderer takes
2.5 GB of the low heap on Linux).

## Mods and custom maps

`+set fs_game mods/<name>` as on Windows. What the native builds add for community mods built with the mod tools:

* any fastfile that is not in `zone\` is looked for in the mod's folder (`<map>.ff`, `<map>_load.ff`,
  `<map>_patch.ff`, localized ones), not only `mod.ff` (log line `mod: fastfile '<zone>' from <path>`), and
  `map` / `devmap` accept a map whose fastfile is there;
* the mod's fastfiles are found under `fs_b` as well as `fs_h`;
* `fs_game` on the command line selects the larger asset pools and adds 256 MB of zone memory, as
  `bo1_mod_zones` does.

* debug asserts do not stop the game while a mod is active (`fs_game` on the command line): retail has none,
  and mod-tools fastfiles trip some (the same asset twice in `mod.ff`, sound bank order). Each one is logged once
  (`mod: assert ignored (retail has none): <file>(<line>): <expression>`), and the code goes on as retail's
  does (a duplicate asset in one zone takes the override path);
* `forceviewmodelanimation <weapon> <anim>` (SP builtin, used by custom staffs) starts KB's fire (2, ADS 7) or
  reload (15) viewmodel anim when the player holds that weapon; `NVG_up` / `NVG_down` do nothing (KB has no
  night-vision viewmodel anims);
* Android: a mod's first start (no `players/mods/<x>/config.cfg`) gets the display-shaped mode and the refresh
  rate as the frame limit, as the base game's first start does; the mod comes from `fs_game` in `args.txt` or the
  Extras screen's `bo1_mod.txt`.

On Android the mod folder goes to `files/game/mods/<name>/` and the arguments into `args.txt`.

Verified on the Odin 3 (Vulkan, 2026-10-14): Waffen der Schönheit 1.1.0 by Sidzzz (`mods/wds`: `mod.ff`
50 MB, `wds.iwd` 886 MB of scripts, images, weapon files) with
`+set bo1_zombies 1 +set fs_game mods/wds +devmap zombie_theater`: `mod.ff` loads in 5.3 s, the map starts with
the mod's weapons, 115-120 FPS at 1280x720. The mod's Ray Gun Mark 3 script calls
`weapondualwieldweaponname("none")` every 0.05 s when no weapon is held, which logs a script error each time
(harmless, retail does the same). Not played with a controller (staffs, perks, box, rounds past 1).

Not done: loose `.csv` /
`.vision` files outside `mod.ff`, mods that rely on a patched exe or DLL beyond what `game_mod` changes in the
fastfile loader.

## Renderers: OpenGL ES 3 and Vulkan

`bo1_renderer` (archived, default 0; read when the renderer starts, so a change applies at the next start):

| value | renderer |
| --- | --- |
| 0 | OpenGL ES 3: the D3D9 shim of `src/web/d3d9` |
| 1 | Vulkan: DXVK's native D3D9 (`libdxvk_d3d9.so`, DXVK 2.6.1, zlib licence) behind `src/web/d3d9/d3d9_vkbridge.cpp` |

The environment variable `BO1_RENDERER=vulkan|gles` overrides the dvar. In the graphics menu the retail
"Full Screen" row is `RENDERER (RESTART)`: OpenGL ES / Vulkan.

How it works: the engine's D3D9 interfaces hold 4-byte pointers, DXVK is an ordinary 64-bit library. The bridge
implements the engine's interfaces and calls DXVK through C functions without pointer types (`src/native/vk/bo1vk.h`,
DXVK side `bo1vk_dxvk.cpp`). A lock whose address does not fit 32 bits is given to the engine as a buffer of its own
heap, copied from DXVK's memory on Lock (unless `D3DLOCK_DISCARD`) and back on Unlock (unless read-only). Display
modes come from the shim's list. The back buffer is presented into the window with its shape kept.

Fallback: when the library is missing, there is no Vulkan driver, DXVK lists no device, or a trial device cannot
be created, the game uses OpenGL ES. Log lines: `renderer: Vulkan (DXVK)`, or
`vulkan: DXVK cannot create a device with this driver (0x...)` followed by
`renderer: Vulkan is not available here, using OpenGL ES`. DXVK needs Vulkan 1.3 and `VK_EXT_robustness2`.

Build: `tools/native/build-dxvk.sh linux|arm64-v8a|x86_64` (inside the container), then `build-p32.sh` /
`build-android.sh` with `-DBO1_NATIVE_DXVK=<dxvk source>` (both scripts pass `/bo1/dxvk/src` when it exists).
`build-apk.sh` puts `libdxvk_d3d9.so` into the APK. On Linux the library is looked for next to the program, then by
the loader's search path; `BO1_DXVK_LIB=<path>` names it. `tools/native/dxvk-smoke.cpp` is a DXVK-only test program.

On Linux the allocator of `native_lowmem.c` serves DXVK, the Vulkan loader and driver, LLVM and libstdc++ from a
heap of their own above 4 GB (recognized by the caller's address); with lavapipe they filled the low heap otherwise.

Verified: x86-64 Linux on Mesa lavapipe (software Vulkan): front end, settings menus, zombie_theater in play,
6-8 FPS at 1280x720 on that driver. Android emulator (x86_64): its Vulkan lacks `VK_EXT_robustness2`; the fallback
to OpenGL ES was seen there. Measured on the Odin 3 (Adreno 830) since: Vulkan 150 to 250 FPS at 1920x1080 (the
README's benchmarks). Not verified: other Android GPUs.

OpenGL ES on the Odin 3: 3.3 s frames in zombie_theater at first (0.3 FPS), all of it updating the engine's dynamic
index buffer: about 4900 small writes a frame into a 3 MB buffer, each 0.65 ms with `glBufferSubData` or an
unsynchronized `glMapBufferRange` (the driver copies the buffer still in use). A small `glBufferData` per draw cost
19 us. Now a draw from a dynamic index buffer passes its indices from the CPU copy as a client-side array (the default
vertex array of OpenGL ES 3; `DrawIndexedPrimitive`, `src/web/d3d9/d3d9_draw.cpp`), and dynamic vertex buffers take
new storage on a DISCARD lock and an unsynchronized write on a NOOVERWRITE lock (`BufferStorage::flush`). Measured:
54 FPS in zombie_theater (18.5 ms frames, GPU at about 25 %), 63 FPS in zombie_pentagon, at 1920x1080. What remains
on the CPU: the dynamic vertex buffer writes (about 10 ms a frame) and linking a program the first time a shader
combination is drawn (up to 250 ms in one frame). `+set bo1_frameperf <ms>` also prints `DRAWPROF` lines (the draw
path's time by stage) for frames of 100 ms or more and every 300th frame.
Not done: letting DXVK's lock memory be used directly (the copy per lock stays), DXVK configuration tuning
(`DXVK_CONFIG`), MSAA under Vulkan checked.

### Refresh rate

`r_displayRefresh` (the graphics menu's refresh rate row) lists the display's refresh rates at its current
resolution on Android (`Bo1Activity` passes them to the loader as `--rates=60,120`, which sets `BO1_REFRESH_RATES`;
elsewhere the list is `60 Hz`). The default is the highest. The chosen rate is asked of the display
(`Bo1Activity.setRefreshRate`: `preferredDisplayModeId`) when the window is made and after `vid_restart`. Log lines:
`native: refresh rate 120 Hz of 2 choices`, `native: refresh rate 120 Hz asked for`. `bo1_refresh_set` (archived,
default 0) records that a configuration written before the setting existed was moved from `60 Hz` to the highest
rate once.

The graphics menu alone decides the frame rate. With `r_vsync 1` (SYNC EVERY FRAME: YES, the default) the frame rate
is the refresh rate (measured on the Thor: 118 FPS at 120 Hz). With `r_vsync 0` the renderer draws as fast as it can
(measured on the Odin 3: about 180 to 230 FPS in Kino der Toten at 1920x1080, GPU at 99 %; on the Thor 128 FPS in the
menu room at 1080p, Turnip v27); the display still shows 120. `com_maxfps` stays 0: a first start sets it to 0
(the engine's default is 85), Settings > EXTRAS has no row for it since 0.5-turnip, and `bo1_maxfps_free` (archived,
default 0) records that a configuration's earlier cap (the removed FRAME RATE LIMIT row, or the display's rate that
earlier versions set on a first start) was set to 0 once: `native: com_maxfps 120 set to 0 once: the graphics menu
decides the frame rate`. Set by hand in the console, `com_maxfps` still caps.

### New pipelines on Vulkan

`bo1_vk_async` (archived, default 1, 0 to 2, read at start; Extras: NEW SHADERS). The Adreno driver has no graphics
pipeline libraries, so a pipeline (shaders + render state) used for the first time takes a compile.

- 1: blended and depth-only draws (effects, decals, shadow maps, the depth pre-pass) are compiled on background
  threads and left out until ready; a draw that writes colour without blending (the base pass of a surface) waits
  for its pipeline (the `BO1_ASYNC2` patch of `build-dxvk.sh`).
- 2: every new pipeline in the background (the default before). A surface is drawn in passes: its base colour, then
  light, glow and decals added over it. With the base pass left out and the added passes drawn, the surface showed
  as one flat colour (orange, blue) for a frame or two. Measured on the Odin 3: the first muzzle flash of a session
  lit the view model's arms and a nearby beam with new pipelines; both showed one flat colour for 2 frames of a
  60 Hz recording; reported in Multiplayer as blue flashes.
- 0: compiled at the draw. Nothing is left out; the frame waits (a stutter).

### Anti-aliasing on Vulkan

`bo1_vk_resolve` (archived, default 2, 0 to 3, read at start): how DXVK resolves the multisampled scene
(`BO1_DXVK_RESOLVE`, the `BO1_RESOLVE` patch in `tools/native/build-dxvk.sh`). The engine resolves the 4x back
buffer into one texture twice a frame, before and after the effects, and draws it back over the scene (bloom).
Measured on the Odin 3 with `r_aaSamples 4` in Kino der Toten:

- 0, DXVK's own choice (the resolve folded into the open render pass): the texture keeps the picture from before
  the effects; no muzzle flash, no explosions.
- 1, a resolve pass of its own: a black scene.
- 2, a fragment shader that reads the samples: right, 60 FPS.
- 3, `vkCmdResolveImage`: right, 57 FPS.

Without anti-aliasing the same scene runs at about 160 FPS (`r_vsync 0`).

### Zombies and Multiplayer settings

The two modes keep separate configurations (`players/config.cfg`, `players/config_mp.cfg`). When the Play screen
starts the other mode, `bo1_renderer`, `r_mode`, `r_aaSamples`, `r_vsync`, `r_displayRefresh`, `com_maxfps` and
`bo1_fpscounter` go along on the command line. `bo1_renderer` defaults to 1 (Vulkan) on Android.

## Touch controls

`src/native/native_touch.cpp` (fingers to pad state and look movement) and `src/client_mp/cl_touch.cpp` (dvars,
drawing). The controls fill the game pad slot while no controller is connected and hide when one is. They show
while a game is being played and no menu is open.

| control | does |
| --- | --- |
| left 45 % of the picture, not on a button | move stick, appears under the finger; pushed to the edge forwards it sprints |
| anywhere else, not on a button | look |
| FIRE (two) | right trigger; the finger holding one also turns the view |
| AIM | left trigger, a toggle |
| JUMP, CROUCH, USE, SWAP | A, B, X (use / reload), Y |
| KNIFE, NADE, TAC | right stick click, right shoulder, left shoulder |
| `^ v < >` | d-pad |
| II, TAB | Start (pause), Back (scores) |

| dvar (archived) | default | range | |
| --- | --- | --- | --- |
| `bo1_touch` | 1 on Android, 0 elsewhere | 0 / 1 | the controls |
| `bo1_touch_sensitivity` | 1.2 | 0.2 .. 5 | view turn per finger travel |
| `bo1_touch_scale` | 1 | 0.5 .. 2 | size of every control |
| `bo1_touch_opacity` | 0.28 | 0.05 .. 1 | |
| `bo1_touch_autofire` | 1 | 0 / 1 | a held FIRE keeps firing a weapon that fires once per pull |
| `bo1_touch_autofire_rate` | 5 | 1 .. 8 | trigger pulls per second of a held FIRE; the weapon's own fire time still applies |

Full-automatic weapons and the minigun get a held trigger as before. Layout: command `touch_edit` (drag the
controls, DONE saves, RESET restores the defaults), `touch_reset`; kept in `<fs_homepath>/touch_layout.txt`
(`name x y size` per line, fractions of the picture). Buttons are drawn as discs made of strips of the white
material: no image of the build's own.

Menus by touch: on the front end's TV menus a swipe moves the selection and a tap chooses; on menus with a pointer
a tap is a click.

Test pad: `finger@<x>@<y>[@<x2>@<y2>][:ms]` in `BO1_TESTPAD` (1280x720 coordinates) with `+set bo1_touch 1`.

Verified: Linux with scripted fingers (stick, aim toggle, 16 pistol shots from one 5 s hold of FIRE); the overlay
drawn in the Android emulator. Not verified: real fingers, more than one finger at a time on a device.
Not done: icons from the game's materials on the buttons, resizing single controls in the editor.

## Menu additions

All applied at run time to the menus of the player's fastfiles (`ui/ui_main.cpp` `UI_ModFov_PatchMenus`,
`ui/ui_shared.cpp` `Item_Action`, `stringed/stringed_hooks.cpp`); the files are not changed.

* Title: "Press Any Key" reads "Tap the screen" on Android without a controller, "Press any button" with one.
* Main menu: Campaign reads COMING SOON and opens a notice. Multiplayer starts the program again in Multiplayer
  mode, on its main menu, with the video settings (`CL_Play_StartMultiplayerMenus`, `client_mp/cl_play.cpp`), as
  retail switches to the multiplayer executable. Zombies > Online opens the join-by-address screen.
* Settings > Graphics: `RENDERER (RESTART)` (was Full Screen), `FPS COUNTER` (was Shader Warming).
* Settings: Voice Chat reads EXTRAS and opens `client_mp/cl_extras.cpp` (also command `extras`): the Play screen,
  touch settings, hold to fire, layout editing, the second screen, FPS
  counter, renderer, bots, mod, player name (Android: on-screen keyboard), join by address, send log.
* A tap on a menu with a pointer moves the pointer first and clicks two frames later (one tap per entry).

### Multiplayer menus without the online service

`bo1_offline_menus` (default 1, 0 or 1; native builds). The retail Multiplayer menus ask for a sign-in to the online
service before PLAY opens (`No network connection detected`). With the dvar on:

- the menu functions `isSignedInToLive`, `areStatsFetched` and `isDemonwareFetchingDone` answer 1
  (`universal/com_expressions_eval.cpp`), so PLAY, Private Match and Create a Class open;
- `getDStat` is compiled in (it was under `BO1_LIVE` and left its arguments on the expression stack:
  `Expected 4 params to function StringTableLookup, found 3`);
- the player's stats are the file `<fs_h>/players/mpstats_offline.bin` (40548 bytes). It is read at the first stat
  operation; without a usable file the stats are new (`mp/stats_init.cfg`: the default classes). `uploadStats`
  writes it (the menus run it when Create a Class closes). Log lines: `offline stats: new`, `offline stats: read`,
  `offline stats: written`;
- every item is unlocked and free (`BG_UnlockablesAllItemsUnlocked`, `BG_UnlockablesAllItemsFree`).

Measured on the Odin 3: PLAY shows Server Browser, Ranked Match, Wager Match, Combat Training, Private Match;
Private Match shows the lobby and START MATCH loads the map (Summit, to the team choice); Create a Class lists
Custom 1 to 10 with the default loadouts and the weapon lists.

Since then (measured on the Odin 3):

- A tap on a row of a Create-a-Class list equips that row's item (`ui_mp/ui_feeders_mp.cpp`: the decompile wrote the
  row number over the item found for the row; tapping MAC11, row 2, named item 2, a pistol, and equipped nothing).
- A match hands out the edited class. The listen server's copy of the host's stats (`client_t::stats`,
  `web/web_stats.cpp`) starts from the menus' stats and is copied back when the server saves; the server takes every
  item for purchased (`GScr_IsItemPurchasedForClientNum`). Log line: `profile: <name> plays with the menus' stats`.
  `+set bo1_statdebug 1` prints every integer stat a script reads (`getdstat: client 0 CacLoadouts customclass1
  primary = 14`).
- The 3D player and weapon preview shows. Its zone, `ui_viewer_mp`, is asked for with flag 2048, which
  `DB_LoadXAssets` dropped; native builds queue it and find it in `zone/Common`. It stays loaded through a match, as
  in retail, and Multiplayer's `main` memory is 64 MB larger for it (`universal/physicalmemory.cpp`).
- Combat Training starts a match with bots (Summit, Regular, 120 FPS). Private Match has no bots, as in retail.
- END GAME returns to the Multiplayer menu.
- `com_maxfps` defaults to 0 (retail Multiplayer: 85, which gave 91 FPS in a match on the 120 Hz display); a
  configuration holding 85 is set to 0 once (`bo1_maxfps_set`). A match runs at 120 FPS with `r_vsync 1`.

- The menus draw one frame a display refresh. Retail draws a menu at most every 16 ms
  (`SCR_ShouldSkipUpdateScreen`, `client_mp/cl_scrn_mp.cpp`): 62 FPS on the 120 Hz display while the main loop ran
  955 times a second. Native builds wait `1000 / refresh rate - 1` ms (7 ms at 120 Hz): 142 FPS with `r_vsync 0`.
  Loading screens keep 33 ms.
- What a match writes comes back to the menus. The host's server buffers start as the menus' buffer of the mode and
  are copied back when the server saves (`web/web_stats.cpp`): Combat Training uses `client_t::stats` and the
  menus' Combat Training buffer, saved in `<fs_h>/players/mpstats_offline_ct.bin` (first run: a copy of the
  Multiplayer stats; log line `offline stats: Combat Training new`); the other modes write every stat to
  `client_t::globalStats`, which goes back to `mpstats_offline.bin`. Measured on the Odin 3: after 2 minutes of
  Combat Training left with LEAVE GAME, 12 bytes of the Combat Training file differ and the Multiplayer file is the
  same. Not measured: a rank gained (no kill was made in the test).
- The Choose Class menu of a Combat Training match names the custom classes (`offline_customclass1` to 5, which
  nothing set, take the names of Create a Class in `CL_InitCGame`).
- Two decompile defects of the preview are corrected on native builds (`ui/ui_viewer.cpp`): the mask of the weapon
  options (an address in place of 0x3FFFFFF) and the weapon name built for attachments and dual wield (the end of
  the name was looked for outside the name).

Not done: Server Browser, Ranked Match and Wager Match need the service. A Private Match gives no rank, as in
retail.

Development aids (native builds): `bo1_dumpmenu <menu>` prints a loaded menu's items, event scripts and conditions
(`ui/ui_utils.cpp`; never commit a dump); `+set bo1_exprdebug 1` prints a failing menu expression; `+set bo1_frameperf <ms>` (command line) prints
`BO1_FRAMEPERF` every 5 s and a `BO1_FRAMEHITCH` line with the phases of every frame of `<ms>` or more;
`<files>/cmd.txt` (`BO1_CMD_FILE`) holds console commands for the running game, read about once a second.
`cg_drawFPS` (Multiplayer's own counter) draws only with `+set bo1_engine_fps 1`; `cg_drawVersion` defaults to 0.

### Front end (the room with the TV menus)

The right stick turns the view while a TV menu is open, as the mouse does (`CG_ShouldUpdateViewAngles`, native builds).
On a touch screen a drag that begins on the right 60 % of the screen turns the view, a swipe on the left 40 % moves
the selection, a tap chooses. The player linked to the chair keeps the link's view limits and cannot crouch: the SP rules for a linked
player (the view clamp of `PM_UpdateViewAngles_RangeLimited`, AllowStand / AllowCrouch / AllowProne, the turn cap) were
gated on `zombiemode`, which the front end runs without; native builds apply them on every SP level
(`Com_SPPlayerRules`, `qcommon/common.cpp`). Measured on the Odin 3: two full upward drags stop with the TV still in
view. Not done: checked against retail's limits; several front end script builtins are not ported
(`game_sp/g_scr_sp_notported.cpp`; `+set logfile 2` prints `<name>: not ported` when one is called, none seen).

After a video restart (`vid_restart`, or a change on the graphics menu: refresh rate, sync, anti-aliasing, video
mode) the TV kept its menus only until 0.4-turnip: the reset gave texture window 0, where the front end's script puts
the TV's menus (`ui3dsetwindow`), the whole screen's placement again (retail Multiplayer's step in
`R_UI3D_CheckRenderTarget`; this engine always counts as Multiplayer), so the menus were drawn outside the TV's part
of the texture. Since 0.5-turnip window 0 keeps the placement it had. Log line of each reset:
`ui3d: render target 1920x1080 recreated; window 0 <x> <y> <w> <h>, texture window placement`.

### Play screen

`client_mp/cl_play.cpp`, command `play`. Rows: MODE, GAME TYPE, MAP, START.

| mode | starts |
| --- | --- |
| ZOMBIES: SOLO | `map <name>` |
| ZOMBIES: HOST | the same with `systemlink 1`, `sv_maxclients 4`, `bo1_expected_players 1`, `bo1_lobby_chars "0 1 2 3"`; the screen shows this device's address |
| MULTIPLAYER: BOTS | an offline match: `splitscreen_playerCount 1`, `splitscreen_botFriends` / `botEnemies` / `botDifficulty`, `g_gametype tdm` or `dm` |
| MULTIPLAYER: COMBAT TRAINING | `onlinegame 1`, `xblive_basictraining 1`, `bot_friends` / `bot_enemies` / `bot_difficulty` |

Maps are the fastfiles of `<fs_basepath>/zone/Common` named `zombie*` or `mp_*` (without `_patch` / `_load`), and
for Zombies also the fastfiles of the active mod's folder. Bots: `bo1_bot_enemies` (0..9, default 6),
`bo1_bot_friends` (0..8, default 5), `bo1_bot_difficulty` (0..3 = easy, normal, hard, fu; default 1), archived.

Zombies and Multiplayer are two modes of one program (`bo1_zombies`, command line only). Starting a map of the
other mode quits and starts the program again with the map's arguments (`src/native/native_extras.cpp`, log line
`native: starting again with: ...`): `execv` on Linux; on Android the arguments go to `files/next_args.txt`, which
the loader uses once, and the activity is restarted (`Bo1Activity.restartApp`). The retail "switch executable"
commands (`startSingleplayer`, Multiplayer's MAIN MENU entry) restart the same way.

### Games on the local network

While the join screen is open it broadcasts `bo1find` to UDP port 28960 every 3 s. A server of this build that
takes players by address (`systemlink 1` or dedicated) answers `bo1here "<map>" <players> <max> "<host name>"`; the
screen lists it as `LAN <address> <map> <players>/<max>` under the recent addresses. Choosing it puts the address on
the line. `tools/native/lan-test.sh` runs a host started from the Play screen and a second game that finds and
joins it.

### Mods

EXTRAS > MOD lists the folders of `<fs_basepath>/mods` and `<fs_homepath>/mods`; the choice is kept in
`<fs_homepath>/bo1_mod.txt` and becomes `+set fs_game mods/<name>` at the next start (`web/web_main.cpp`), unless
the command line names one. A on the row restarts with it.

### Send log

EXTRAS > SEND LOG, Android: the last 120 000 bytes of `bo1.log` as text through the system's share sheet.

Android video mode: the loader reports the display (`BO1_DISPLAY_SIZE`), the mode list gains the display's shape
at 540 / 720 / 900 / 1080 / 1440 lines, and a first start (no `players/config.cfg`) uses the 720-line one
(`bo1_boot: display 2340x1080, default mode 1560x720`), so the picture fills the screen. Settings > Graphics >
Video Mode changes it.

Verified on Linux: Multiplayer > Play > START restarts into a bot match on mp_nuked; a host started from the
Play screen is found by a second game's join screen and joined (two players in zombie_theater); the mod file gives
`fs_game` at the next start. Verified in the Android emulator: the restart into a multiplayer match by touch, the
share sheet with the log, one tap per menu entry. Not verified: a mod with a custom map in the Play screen's list,
discovery between two real devices (it needs a network that passes broadcasts), Combat Training started from the
screen.

Not done: join codes, the multiplayer front end's own Play entry (disabled without the online service), a server
browser beyond the local network, attaching the whole log file (the share sheet gets its end as text).

After Game Over a Zombies map starts again by itself in every mode (`coop: zombies server: starting <map> again`),
not only on a dedicated server.

## First run on a device (AYN Odin 3, Snapdragon 8 Elite, Adreno 830, Android 15)

Measured, Vulkan, 1280x720, zombie_theater in play, 120 Hz display mode: 120 FPS, 5 % low 108, 1 % low 94.

What that run showed and what was changed for it:

* Swap chain: Android reports `VK_SUBOPTIMAL_KHR` for every image of a surface with a non-identity transform (the
  panel is portrait by nature). DXVK made a new swap chain per frame (402 in one log) until the driver crashed.
  `build-dxvk.sh` patches the presenter to treat it as success on Android.
* Refresh rate: the activity asks for the display's fastest mode (`preferredDisplayModeId`) and passes
  `--refresh=<Hz>` to the loader, which used it as `com_maxfps` on a first start (the engine's default is 85). Since
  0.5-turnip a first start sets `com_maxfps 0` and the graphics menu decides (see "Refresh rate").
* Stutter: the driver has no `VK_EXT_graphics_pipeline_library` (`DXVK: Graphics pipeline libraries not supported`),
  so DXVK compiled each new pipeline inside the draw that first needed it. `build-dxvk.sh` patches DXVK to compile
  such pipelines on its worker threads and leave the draws out until they are ready. `BO1_DXVK_ASYNC=0` in the
  environment restores the waiting. Exercised on Linux with `DXVK_CONFIG="dxvk.enableGraphicsPipelineLibrary = False"`;
  not yet measured on the device.
* Sound: on PC most effects are xWMA (`native: sound: not played: format 7, ...` in older logs). The mixer decodes
  them with FFmpeg's WMA 2 decoder when the build has it: `tools/native/build-ffmpeg.sh` (libavcodec with that one
  decoder, static, LGPL), cmake `-DBO1_NATIVE_FFMPEG=<dir>`.
* Multiplayer crash when a map started (`Destructible_ReadGameState`, and the server thread at the same moment):
  not reproduced on x86-64, in the emulator, or under qemu-user. The loopback and deferred packet queues
  (`qcommon/net_chan_mp.cpp`) are lock-free and relied on x86's memory ordering; their readers now have a full
  barrier. Whether that was the cause is not verified: it needs the device.
* Crash reports: the AArch64 handler prints the registers and the `/proc/self/maps` lines of the instruction, the
  address and the stack. The log of the previous start is kept as `bo1.prev.log`.
* Menus: the pointer is not drawn on Android unless a real mouse moved in the last 5 s, and rests in a corner.

Open after that run:

* Muzzle flashes and explosion effects were reported missing on the device (Vulkan). They draw on x86-64 Linux with
  both renderers and in the AArch64 build under qemu-user (`tools/native/fx-test.sh`, `fx-arm64.sh`). Not explained.
* OpenGL ES was reported slow on the device. Not measured.
* The retail multiplayer menus for classes (`menu_xboxlive_basictraining`, `cac_main`, ...) are in the fastfiles
  and open, but crash in the first stats lookup: 50 calls of `DDL_MoveTo` in `bgame/bg_unlockable_items.cpp`,
  `bg_emblems.cpp`, `live/live_stats.cpp`, `live/live_combatrecord.cpp` and others pass a depth larger than the
  number of path names they give (the decompiler dropped variadic arguments). The names have to be recovered from
  the stats definition before those menus can work.
