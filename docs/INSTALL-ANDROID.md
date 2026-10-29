# Installing on Android (NOT part of the original game)

This guide takes you from nothing to playing *Call of Duty: Black Ops* Zombies and Multiplayer on an Android device.
The app is the engine only. **You need your own PC copy of Black Ops from Steam**: the game's files are copied from it
to the device, and the app does not run without them.

## 1. What you need

| | |
| --- | --- |
| Game | *Call of Duty: Black Ops* on Steam (PC), installed. Zombies maps from the DLC packs appear only if you own and installed them. |
| Device | Android 8.0 or newer, 64-bit ARM (arm64-v8a). A recent Snapdragon chip (Adreno GPU) is recommended. |
| Graphics | Vulkan 1.3 for the default renderer. Without it the game uses OpenGL ES 3, about a third as fast (54 to 63 FPS on the tested device). |
| Memory | 6 GB of RAM at least, 8 GB or more recommended. |
| Storage | About 12 GB free on the device's internal storage (the game folder is about 11 GB). |
| PC | Windows, Linux or macOS with a USB cable for the device, to copy the files once. |
| Controls | A controller (built-in handheld controls, Bluetooth or USB), or the on-screen touch controls. |

Tested on: AYN Odin 3 (Snapdragon 8 Elite, Adreno 830, Android 15). Other devices are expected to work but are not
tested; reports are welcome (see "Problems"). The README has the full requirements (minimum and recommended), what
each renderer supports, and benchmarks.

## 2. Install the app

1. Download `bo1-port-<version>.apk` from the repository's **Releases** page. Only install it from there; nobody
   else ships builds of this project.
2. Open the file on the device. Android asks you to allow installing apps from that source (the browser or the
   file manager): allow it for that app only, then tap **Install**.
3. Start the app once. It shows "Game files not found" and the folder it expects. Leaving it now is fine: starting
   it once is what creates that folder.

Updating later: install the new APK **over** the old one. **Never uninstall the app to update it.** Android deletes
the app's folder when an app is uninstalled, and that folder holds your 11 GB of game files, settings and progress.

## 3. Find your game files on the PC

In Steam: **Library**, right-click **Call of Duty: Black Ops**, **Manage**, **Browse local files**. The folder that
opens is usually

```
C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops
```

It contains `localization.txt`, `zone`, `main`, `players` and others. If your library also shows **Call of Duty:
Black Ops - Multiplayer**, install it too: it puts the Multiplayer files into the same folder. The game should be
updated by Steam (the latest version), and you should have started it at least once on the PC.

## 4. Copy the game files to the device

The folder on the device is

```
Internal storage/Android/data/org.bo1port.zombies/files/game/
```

(`/sdcard/Android/data/org.bo1port.zombies/files/game/` for adb). Copy the **contents** of `Call of Duty Black
Ops` into `game`, so that `game/localization.txt` and `game/zone/` exist. Do not copy the folder itself into `game`;
that gives `game/Call of Duty Black Ops/...`, which the app does not find.

### Option A: USB cable and the file manager (Windows)

1. Unlock the device and connect it. In the USB notification choose **File transfer**.
2. In Windows Explorer open the device, then **Internal storage > Android > data > org.bo1port.zombies > files**.
   Create the folder `game` if the app did not.
3. Copy everything inside `Call of Duty Black Ops` into `game`. Keep the device unlocked and awake; the copy takes
   10 to 40 minutes.

If **Android/data** looks empty or the app's folder is missing over USB, start the app once more on the device, then
unplug and reconnect the cable. Some phones hide `Android/data` from the PC entirely; use option B then.

### Option B: adb (any PC)

1. Install Android's platform tools (adb) from https://developer.android.com/tools/releases/platform-tools.
2. On the device, enable **Developer options** (Settings > About > tap **Build number** seven times), then
   **USB debugging**. Connect the cable and accept the prompt on the device.
3. Run (with your own path):

```
adb push "C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops\." /sdcard/Android/data/org.bo1port.zombies/files/game/
```

The `\.` at the end copies the contents, not the folder.

### Copying on the device itself

Android 11 and newer stop normal file manager apps from writing into `Android/data`. Copying the files to the device
first (download folder, SD card) and moving them from there only works with a file manager that has that access on
your device, or with root. The PC options above always work.

## 5. Play

Start the app. The first minutes can stutter while the Vulkan renderer prepares the shaders it meets for the first
time; that settles.

- Without extra settings the app starts **Zombies**: the main menu is the room with the TV. **Solo** starts a map.
- **Multiplayer**: from the main menu choose **Multiplayer**. The app restarts into the Multiplayer game (that is
  expected) and shows its main menu. **PLAY** has **Combat Training** (bots, with rank, unlocks and Create a Class)
  and **Private Match**. Settings > EXTRAS > PLAY: MAPS AND MODES starts a bot match on any map directly.
- **Back to Zombies** from Multiplayer: the Play screen (Settings > EXTRAS > PLAY: MAPS AND MODES), mode **ZOMBIES: SOLO**.
- Progress (rank, classes, unlocks) is saved on the device. Combat Training keeps its own progress, as in the
  original game.

### Controls

- **Controller**: works as on a console. The on-screen controls hide while a controller is connected.
- **Touch**: with no controller the on-screen controls appear in a game. The left part of the screen is the move
  stick, the rest turns the view. Other buttons are FIRE (two), AIM, JUMP, CROUCH, USE, SWAP, KNIFE, NADE, TAC, the
  d-pad arrows, pause and scores. Settings > EXTRAS has touch sensitivity, size, opacity, hold-to-fire and the layout
  editor. In menus, a tap is a click; on the TV menus, a swipe moves the selection and a tap chooses.

### Settings worth knowing

- **Settings > Graphics > Video Mode**: the default is 720 lines in your screen's shape. Higher looks sharper and
  costs frame rate.
- **Settings > Graphics > RENDERER (RESTART)**: Vulkan (default, fastest) or OpenGL ES 3 (for devices without
  Vulkan 1.3, which use it by themselves, or when Vulkan shows problems; about a third as fast).
- **Settings > EXTRAS**: FRAME RATE LIMIT (30 to no limit), FPS COUNTER, bots, mod, PLAYER NAME, JOIN A GAME BY
  ADDRESS, SEND LOG.
- Zombies and Multiplayer keep separate settings files. The graphics settings carry over when the app switches mode.

## 6. Playing with friends (local network or by address)

All players need this app (or another build of this project) of the same version. The original PC game cannot join.

- **Host a Zombies game**: Play screen, mode **ZOMBIES: HOST**, pick a map, START. The screen shows your device's
  address.
- **Join**: main menu **Zombies > Online**, or Settings > EXTRAS > JOIN A GAME BY ADDRESS. Games on the same Wi-Fi are listed (`LAN ...`);
  otherwise type the host's address. Over the internet, the host must forward UDP port 28960 on their router.
- Up to 4 players get the original characters.

Not available: the online Server Browser, Ranked Match and Wager Match (they need Activision's online service), and
joining by code.

## 7. Community mods and custom maps

Mods made with the Black Ops mod tools (a folder with `mod.ff` and usually an `.iwd`) can run, Zombies ones first.
Tested: Waffen der Schönheit 1.1.0 (a Kino der Toten mod), 115-120 FPS on an Odin 3. Other mods are untested; a mod
that needs a patched game exe or extra DLLs on PC will not work.

1. Copy the mod's folder (on PC `Call of Duty Black Ops\mods\<name>\`, or the folder inside the mod's download)
   to `Android/data/org.bo1port.zombies/files/game/mods/<name>/`.
2. Settings > EXTRAS > MOD: pick it; the app restarts with it. Then start the map the mod changes or adds from the
   menus. Or start it directly with `files/args.txt` (one line), for example
   `+set bo1_zombies 1 +set fs_game mods/wds +devmap zombie_theater`.
3. A mod keeps its own settings (renderer, video mode, FPS counter) apart from the base game's.

If a mod stops with an error, send the log (below) with the mod's name and where to get it.

Settings > EXTRAS > MOD still lists every folder in `files/game/mods/` and starts the game with the one you pick. The
script mods made for this port (horde, nightmare, mapkit, noperks, sandbox, zinfo, coop) are not in this fork's
repository: get them from [Zephelinn's repository](https://github.com/Zephelinn/BO1-Ascension-ARM64/tree/main/mods) and copy a mod's
folder to `files/game/mods/<name>/` the same way.

## 8. Problems

| symptom | what to do |
| --- | --- |
| "Game files not found" | `game/localization.txt` must exist. Check you copied the *contents* of the folder (step 4). |
| The app closes on start | Check the free space and that the copy finished; copy again over the old files. Then send the log (below). |
| Black screen or broken graphics | Switch the renderer to OpenGL ES 3, and report it with the log. If the menu is unreachable, create `files/args.txt` containing `+set bo1_zombies 1 +set bo1_renderer 0` (one line), start once, then delete the file. |
| Low frame rate | Lower Video Mode; check EXTRAS > frame rate limit. |
| No sound effects in a few places | Report it with the log. |
| Multiplayer menu items are disabled | Server Browser, Ranked and Wager need the online service and stay disabled. Use Combat Training or Private Match. |

**Send log**: Settings > EXTRAS > SEND LOG shares the end of the log as text. The full log is
`Android/data/org.bo1port.zombies/files/bo1.log` (`bo1.prev.log` is the start before). Open an issue on the
repository with it, your device and what you did.

## Advanced: start options

`files/args.txt` (one line) adds engine arguments to every start, for example `+set bo1_zombies 1 +devmap
zombie_theater` to go straight into a map. Delete the file to go back to normal starts. `docs/android-port.md`
describes how the port works, and the dvars and commands it adds.

## Removing it

Uninstall the app. That deletes `Android/data/org.bo1port.zombies` with the game files and your progress. Copy
`files/players` off the device first if you want to keep the progress.
