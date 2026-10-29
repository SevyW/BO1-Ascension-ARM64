<div align="center">

<img src="images/thor-menu-live.gif" width="880" alt="The Black Ops Zombies main menu running on an AYN Thor at 1920x1080: the room sways and the TV's menu highlight moves from Coming Soon down to Settings and back, at about 118 FPS">

# BO1 Zombies for Android

**Created by [Sevy](https://github.com/SevyW)**

*Call of Duty: Black Ops* (2010) **Zombies**, running natively on Android handhelds and phones. Vulkan rendering with **your choice of GPU driver** (Mesa Turnip for Adreno), controller and touch controls, and **dual-screen controls** for devices like the AYN Thor.

[![Release](https://img.shields.io/github/v/release/SevyW/bo1-zombies-android?label=release&color=6e5494)](https://github.com/SevyW/bo1-zombies-android/releases/latest)
![Downloads](https://img.shields.io/github/downloads/SevyW/bo1-zombies-android/total)
![Platform](https://img.shields.io/badge/platform-Android%20arm64-3ddc84?logo=android&logoColor=white)
![Renderer](https://img.shields.io/badge/renderer-Vulkan%20(DXVK)-a41e22?logo=vulkan&logoColor=white)
![GPU](https://img.shields.io/badge/GPU-Adreno%20%2B%20Turnip-c2292f)
![Status](https://img.shields.io/badge/tested%20on-AYN%20Thor-3fa35b)
![Built with](https://img.shields.io/badge/vibe--coded%20with-Claude-d97757?logo=anthropic&logoColor=white)

**[⬇️ Download](https://github.com/SevyW/bo1-zombies-android/releases/latest)** · [Install](#-install) · [GPU drivers](#-gpu-drivers-adreno) · [Settings](#%EF%B8%8F-settings) · [Dual-screen controls](#%EF%B8%8F-dual-screen-controls) · [Help](#-help)

</div>

> [!IMPORTANT]
> **You need your own copy of *Call of Duty: Black Ops* for PC (Steam).** This app is the game engine only. No game files are in this repository or in the app: you copy your Steam install to the device once.

<div align="center">

| 📱 **Native** | 🎨 **Correct graphics** | ⚡ **~120 FPS** | 🕹️ **Dual screen** |
|:---:|:---:|:---:|:---:|
| an Android app,<br>not an emulator | Mesa Turnip fixes the flat<br>models Adreno 740 gets | at 1080p on an AYN Thor<br>(Snapdragon 8 Gen 2) | the bottom screen becomes<br>a trackpad to aim and fire |

</div>

## 🎬 See it work

<table>
  <tr>
    <td width="50%"><img src="images/thor-kino-pad.gif" alt="Kino der Toten on an AYN Thor: the view pans as a finger slides on the bottom-screen look pad"></td>
    <td width="50%"><img src="images/thor-dual-screen.jpg" alt="Both AYN Thor screens: the game's main menu room on top, the look pad with HIDE and REMAP on the bottom"></td>
  </tr>
  <tr>
    <td><sub><b>Kino der Toten</b>, aiming with the bottom-screen look pad.</sub></td>
    <td><sub><b>Both screens of the AYN Thor</b>: the game on top, the look pad underneath.</sub></td>
  </tr>
</table>

<sub>Recorded on the device. The FPS counter is cropped out of the gameplay clip, because recording slows the GPU.</sub>

## ✨ Features

| | |
| --- | --- |
| 🧟 **Zombies** | Every Zombies map in your install (the DLC maps if you own them). Solo, or co-op with friends on the same Wi-Fi or by address, up to 4 players. |
| 🎨 **Vulkan, your driver** | Direct3D 9 is translated to Vulkan (DXVK). On Adreno GPUs you can load **Mesa Turnip** instead of the phone's own driver: one folder and one text file, no root. |
| 🕹️ **Dual-screen controls** | On a device with a second screen, the bottom screen is a look pad. Press your thumb flat to fire, with ten vibration feels, and remap every button while you play. |
| 🎮 **Controllers and touch** | Built-in, Bluetooth and USB controllers work as on a console. Without one, on-screen touch controls with a layout editor. |
| 🖥️ **Up to 1080p and 120 Hz** | Resolution, refresh rate and frame rate are set from one Graphics page. |
| 🔫 **Multiplayer (experimental)** | Bot matches, Combat Training and Private Match, from the original port. Not yet tested with this app's Turnip build. |
| 🧩 **Mods** | Community Zombies mods made with the Black Ops mod tools (Settings > EXTRAS > MOD). |

Not available: the Campaign, and online matchmaking (it needs Activision's servers).

## 📱 Requirements

| | |
| --- | --- |
| Device | Android 8.0 or newer, 64-bit ARM. A recent Snapdragon (Adreno GPU) is recommended. |
| Graphics | Vulkan 1.3 for the Vulkan renderer. Without it the app uses OpenGL ES 3, which is slower. |
| Memory | 6 GB of RAM at least, 8 GB or more recommended. |
| Storage | About 12 GB free on internal storage. |
| Game | *Call of Duty: Black Ops* for PC on Steam, installed and updated. |
| A computer | Only to copy the game files once (USB cable). |

## 📦 Install

### 1. Install the app

Download the APK from the **[latest release](https://github.com/SevyW/bo1-zombies-android/releases/latest)**, open it on the device and allow the install. Then **start the app once**: it says the game files are missing, and creates its folder.

### 2. Copy the game files

On your PC, find the game. In Steam: **Library > Call of Duty: Black Ops > Manage > Browse local files**. It's usually:

```
C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops
```

Copy **everything inside** that folder to this folder on the device:

```
Internal storage / Android / data / org.bo1port.zombies.turnip / files / game /
```

When it's right, the `game` folder looks like this:

```
game/
├── localization.txt
├── main/
├── players/
├── zone/
└── …
```

**How to copy:**
- **With a USB cable (easiest).** Connect the device, choose **File transfer** in its USB notification, and open the path above in your computer's file manager (Windows Explorer, or Android File Transfer on a Mac). Create the `game` folder if it's missing. The copy takes 10 to 40 minutes; keep the device awake.
- **On the device itself.** Android 11 and newer only let some file managers write into `Android/data`, so copying on the device works only with one that can, or with root.

> [!TIP]
> Copy the *contents* of `Call of Duty Black Ops`, not the folder itself. If you end up with `game/Call of Duty Black Ops/…`, the app won't find the files.

### 3. Play

Start the app. The main menu is the room with the TV: choose **Zombies**, then **Solo**. The first minutes in a map can stutter while shaders are prepared; that settles.

> [!WARNING]
> **Updating: install the new APK over the old one. Never uninstall to update.** Android deletes an app's folder when the app is uninstalled, and that folder holds your game files, settings and progress.

## 🔌 GPU drivers (Adreno)

Some Adreno devices draw the game wrong with their own driver. On the AYN Thor (Adreno 740, Qualcomm driver 0.676.53), every model came out flat grey and the TV menus were missing. A **Mesa Turnip** driver fixes that, and is faster:

<p align="center">
  <img src="images/thor-stock-vs-turnip.jpg" width="820" alt="The Zombies main menu on an AYN Thor: on the left the stock Qualcomm driver draws the TVs as flat grey shapes with no menu, on the right Turnip v27 draws them correctly at 119 FPS">
</p>

**To use a Turnip driver:**
1. Download a Turnip driver package (a `.zip` with a `meta.json` and a `.so` file). Builds by [StevenMXZ](https://github.com/StevenMXZ/Adreno-Tools-Drivers) and [MrPurple](https://github.com/MrPurple666/purple-turnip) work.
2. Unzip it into its own folder under `files/driver/`, for example `files/driver/v27/`.
3. Create a text file `files/driver.txt` that contains just the folder's name: `v27`.

```
org.bo1port.zombies.turnip / files /
├── driver.txt          ← contains: v27
├── driver/
│   └── v27/
│       ├── meta.json
│       └── libvulkan_freedreno.so
└── game/
```

Without `driver.txt`, the app uses the device's own driver, as usual. If a driver doesn't load, the app falls back to the device's driver by itself. Each start writes which driver it used to `files/bo1.log` (a line starting with `vulkan driver:`).

**Tested on an AYN Thor** (main menu, 1280x720, no frame cap):

| Driver | Picture | FPS |
| --- | --- | ---: |
| Qualcomm 0.676.53 (the device's own) | ❌ flat models, no TV menu | 96 |
| MrPurple T30 (Mesa 26.3.0) | ✅ correct | 104 |
| MrPurple T28 (Mesa 26.2.0) | ✅ correct | 86 |
| **StevenMXZ v27.0.0-R1 (Mesa 27.0.0)** | ✅ **correct** | **119** |

## ⚙️ Settings

Everything about picture and frame rate is on **Settings > Graphics**:

| Setting | What it does |
| --- | --- |
| **VIDEO MODE** | The resolution. The app starts at 720p; 1920x1080 holds about 120 FPS on an AYN Thor with Turnip v27. |
| **SCREEN REFRESH RATE** | How often the screen refreshes, for example 120 Hz or 60 Hz. |
| **SYNC EVERY FRAME** | **Yes**: the frame rate follows the refresh rate (120 Hz gives 120 FPS, 60 Hz gives 60 FPS and saves battery). **No**: no cap, as fast as the GPU goes. |
| **ANTI-ALIASING** | Smoother edges, but heavy on a handheld: 4x costs about a third of the frame rate at 1080p. Off is recommended. |
| **RENDERER (RESTART)** | Vulkan (fastest) or OpenGL ES 3 (for devices without Vulkan 1.3). |

**Settings > EXTRAS** has the FPS counter, the touch controls (sensitivity, size, opacity, layout editor), the dual-screen controls switch, mods, and SEND LOG.

## 🕹️ Dual-screen controls

On a device with a second display, like the AYN Thor's bottom screen, the app puts a look pad there by itself. On a phone with one screen nothing changes.

<p align="center">
  <img src="images/thor-bottom-screen.jpg" width="880" alt="Three views of the bottom screen: the look pad, the REMAP list of buttons and actions, and the PADS tab with the ten click feels">
</p>

| | |
| --- | --- |
| 👆 **Look** | Slide a finger: the view turns like a mouse. Sensitivity and acceleration are adjustable. |
| 🔫 **Fire** | Press your thumb flat on the glass. The screen has no pressure sensor, so the press is read from how much the finger's contact grows. Double tap or press-and-hold also work, or turn the click off so the pad only aims. |
| 📳 **Feel** | Ten vibrations to choose from for the press, or none. |
| 🎛️ **REMAP** | Rebind every button of the built-in controller and the pad's press. Changes apply at once, with presets per mode and a reset to the console layout. |
| 🙈 **HIDE** | Shrinks it to a small button, so you can use the launcher underneath. Settings > EXTRAS > SECOND-SCREEN CONTROLS turns it off. |

The game keeps the controller while you touch the bottom screen. Details: [docs/second-screen.md](docs/second-screen.md).

## ❓ Help

| Problem | What to do |
| --- | --- |
| "Game files not found" | `files/game/localization.txt` must exist. Check you copied the *contents* of the Steam folder. |
| Models are flat grey, the TV menu is missing | Your GPU driver draws the game wrong: use a [Turnip driver](#-gpu-drivers-adreno). |
| Low frame rate | Lower VIDEO MODE, and turn ANTI-ALIASING off. |
| Frame rate stuck at 60 | SCREEN REFRESH RATE is 60 Hz with SYNC EVERY FRAME on: set 120 Hz, or sync off. |
| The app closes on start | Check the free space and that the copy finished, then send the log. |

**Logs:** Settings > EXTRAS > SEND LOG shares the end of the log. The full log is `files/bo1.log` in the app's folder (`bo1.prev.log` is the start before).

**Something not working?** [Open an issue](https://github.com/SevyW/bo1-zombies-android/issues/new/choose) with your device, the driver you use, what happened, and the log.

The full guide, with co-op, Multiplayer, mods and start options: **[docs/INSTALL-ANDROID.md](docs/INSTALL-ANDROID.md)**.

## 🛠️ For developers

| | |
| --- | --- |
| [docs/android-port.md](docs/android-port.md) | How the Android version works, its settings and engine variables, building it |
| [docs/second-screen.md](docs/second-screen.md) | The dual-screen controls |
| [docs/controllers.md](docs/controllers.md) | Controllers, default layout, binds |
| [docs/multiplayer.md](docs/multiplayer.md) | Co-op and Multiplayer |
| [STATUS.md](STATUS.md) | The exact build steps used for the releases |
| [CLAUDE.md](CLAUDE.md) | How the engine and the repository are organized |

**Building** (Linux x86-64, repository at `/bo1/src`):

```
bash tools/native/android-setup.sh
bash tools/native/build-ffmpeg.sh arm64-v8a
bash tools/native/build-p32.sh
bash tools/native/build-android.sh
bash tools/native/build-dxvk.sh arm64-v8a
bash tools/native/build-adrenotools.sh
bash tools/native/build-apk.sh
```

> [!NOTE]
> **Created by [Sevy](https://github.com/SevyW), vibe-coded with Claude.** This fork's additions (GPU driver choice, dual-screen controls, the fixes) were written by [Claude](https://claude.ai) through Claude Code, in a back-and-forth with me: I set the goals and made the decisions; Claude wrote, built, debugged and tested the code on my AYN Thor.

## 🙏 Credits

- **[Zephelinn](https://github.com/Zephelinn/BO1-Ascension-ARM64)**: the Android port this app is built on.
- **[spacemandev-git](https://github.com/spacemandev-git/bo1-zombies-decompiled)**: the decompiled engine with Zombies on Windows and in the browser.
- **[MisaDev4](https://github.com/MisaDev4/bo1-zombies-decompiled)**: the original Black Ops 1 Zombies decompilation.
- **[bylaws/libadrenotools](https://github.com/bylaws/libadrenotools)**: loading custom GPU drivers on Android.
- **Mesa Turnip**, and the people who build it for Android: [StevenMXZ](https://github.com/StevenMXZ/Adreno-Tools-Drivers), [MrPurple](https://github.com/MrPurple666/purple-turnip).
- **[DXVK](https://github.com/doitsujin/dxvk)**, **SDL2**, **FFmpeg**: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## ⚖️ Legal

- Not affiliated with or endorsed by Activision, Microsoft or Xbox. *Call of Duty* and *Black Ops* are trademarks of Activision Publishing, Inc. See **[NOTICE.md](NOTICE.md)**.
- The engine source is decompiled from the original game and is **not offered under an open-source licence**. Third-party components and their licences are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- The game's own files are never in this repository or the app. You copy your own install to your device.
- Non-commercial. There's no donation link on purpose.
