# Second-screen controls (NOT part of the original game)

On a dual-screen Android device the second display becomes a trackpad for looking, with a remapping screen for every
button, while the game runs on the main display. Pressing a finger flat against the glass fires. Made for the AYN Thor
(lower screen: display 4, "Screen-2", 1080x1240, 290 dpi, `FLAG_PRESENTATION`). On a device with one display nothing
changes.

Status (2026-10-29): runs on the AYN Thor (display 4 found, the game keeps focus, the pad turns the view, an
injected size press fires and releases). The feel of the press and the look speed are still being tuned by hand.

## Start it

Start the game. If the device has a display meant for presentations (`Display.FLAG_PRESENTATION`) other than the
game's, the controller appears on it by itself and covers the app that was there (the Thor's launcher). Settings >
EXTRAS > SECOND-SCREEN CONTROLS turns it off and on (`bo1_second_screen`, below).

`files/bo1.log` shows, in this order (the Java lines are held until the engine is running, then written together):

```
second screen: natives registered
second screen: controller on display 4 "Screen-2" 1240x1080 rotation 1 290 dpi
second screen: layout 1240x1080 pad 22,97-1218,1058 HIDE 22,11-295,75 REMAP 945,11-1218,75
second screen: engine connected
second screen: default binds for the second screen's keys (zm)
second screen: bind AUX1 "+gostand"
...
```

The layout line gives each control's box as `left,top-right,bottom` in the display's pixels. It follows the display's
real size and orientation. The default-binds lines appear once per mode (Zombies `zm`, Multiplayer `mp`), and only if
none of the second screen's keys is bound yet. The same lines go to logcat under the tag `bo1`.

## Leave it

- HIDE (top left) shrinks the controller to a small PADS button in the top right corner; the display's own app (the
  launcher) is usable around it. PADS brings the controller back.
- Settings > EXTRAS > SECOND-SCREEN CONTROLS: Off removes it (`second screen: bo1_second_screen 0`,
  `second screen: controller removed`) until it is turned on again. The setting is saved like the other EXTRAS
  settings (one per mode: Zombies and Multiplayer keep separate configs).
- It goes away when the game goes to the background and comes back with it.

## The controls

One trackpad fills the screen under a bar with HIDE and REMAP.

| control | does | default |
| --- | --- | --- |
| the pad | turns the view as a mouse does (the engine's mouse path). Sensitivity and acceleration | look |
| the pad's click | key `AUX6` | `+attack` (fire) |

The click is, by default, a **size press**. The panel has no pressure sensor, but it reports the contact's size
(`ABS_MT_TOUCH_MAJOR`, 0-255; Android's `AXIS_SIZE` is that over 255). Measured on the Thor:
- a finger resting lightly: 2-5
- a thumb pressed flat: 11-13, climbing there in about 0.2-0.3 s.

How a press is detected (`SecondScreenMath.Press`):
- **Press:** a quick rise to at least 1.8x the finger's relaxed size (its smallest since the last press) and at least the press depth above it (default 5).
- **Release:** when the size falls halfway back. The finger can press again without lifting.
- **Slow rise:** a rise slower than 0.4 s is a finger rolling flatter, not a press, and becomes the new relaxed size.
- **Freeze:** flattening a finger moves its center, so the view holds still while the size climbs toward a press and for 120 ms after a press or a release. While the finger stays pressed (firing), it aims again.

The other click methods are a double tap (the second touch within 300 ms of the first, near it) or a finger held still for the hold time. Either lasts until the finger lifts. A haptic click comes with each press.

The pad's key is one nothing else uses on Android, bound like any key (`bind AUX6 "+attack"`) and saved with the
other binds in `files/home/players/config.cfg` (`config_mp.cfg` in Multiplayer).

In menus the pad still sends mouse movement but not a pointer position. In the front end's room with the TV menus
it turns the view as a mouse does; menus with a pointer ignore it. Menus are driven by the controller or a tap on
the main display, as before.

## Remapping

REMAP (top right) opens three tabs:

- BUTTONS: every input (A, B, X, Y, LB, RB, LT, RT, L3, R3, Start, Select, the d-pad, the trackpad press) with its
  action. A tap opens the list of
  actions; a choice is bound at once (`bind` / `unbind` through the engine's command buffer, logged as
  `second screen: bind ...`). The actions: fire, aim down sight (hold / toggle), jump, crouch / prone, crouch toggle,
  prone toggle, reload / use, reload, use, knife, sprint / hold breath, sprint, lethal and tactical grenade, next and
  previous weapon, action slots 1-4, lean left / right, scoreboard, pause menu, nothing.
- PADS: the settings below.
- PRESETS: Reset to console default (the console's "Default" pad layout of docs/controllers.md and the defaults
  above), and three saved slots per mode (Zombies and Multiplayer separately): Save current as Custom 1-3, Load
  Custom 1-3.

DONE returns to the pads. Nothing on these screens needs the keyboard: the second screen's window never takes focus,
so the game keeps the controller.

## Settings (files/controls.json)

| setting | default | range |
| --- | --- | --- |
| look sensitivity (view per finger pixel, as mouse counts) | 1.0 | 0.1-5 |
| acceleration: extra look for fast swipes, `sensitivity * (1 + accel * min(speed / 1000 px/s, 3))` | 0.5 | 0-2 |
| pad click (`click`) | press | press, double tap, press and hold, off (the pad only looks) |
| press depth (`pressDepth`): how far above the relaxed size a press must rise, in raw size units | 5 | 3-10 |
| hold time | 350 ms | 150-800 ms |
| click feel (`haptics`, `clickFeel`): off, or one of ten vibrations on a press (soft tick, light click, crisp click, low tick, soft thud, double tick, click with a tick on release, trigger pull, heavy click, short buzz); on the PADS tab a tap plays it | light click | off, 1-10 |
| dim after (the controller fades to 30 % until the next touch) | 5 s | 0 (never)-30 s |

| dvar | default | range | |
| --- | --- | --- | --- |
| `bo1_second_screen` | 1 | 0 / 1, saved | the controller on the second display (Settings > EXTRAS) |

`controls.json` also records which modes already got the default binds (`defaultsApplied`) and the saved presets.

## How it works

| file | what |
| --- | --- |
| `android/java/org/bo1port/zombies/SecondScreen.java` | picks the display (`DisplayManager.getDisplays(DISPLAY_CATEGORY_PRESENTATION)`, the first with `FLAG_PRESENTATION` that is not the game's), shows a `Presentation` there with `FLAG_NOT_FOCUSABLE` and `FLAG_NOT_TOUCH_MODAL`, follows displays coming and going, the activity's onStart / onStop, the engine's state; haptics; default binds |
| `SecondScreenView.java` | draws the pad, follows its finger (position and contact size, batched samples included), the click; redraws only on a change |
| `SecondScreenRemap.java` | the remapping screen |
| `SecondScreenMath.java`, `SecondScreenBindings.java` | layout, gesture maths, inputs, actions and presets, without Android classes (tested on the build host: `tools/native/test-second-screen.sh`) |
| `SecondScreenSettings.java` | `files/controls.json` |
| `Bo1Activity.java` | the `ss*` native methods, `secondScreenState` / `secondScreenBinds` from the engine, onStart / onStop |
| `src/native/native_second_screen.cpp` | registers the native methods on the activity's class (libbo1.so is not loaded by the Java class loader), keeps what the JNI calls carry, and once per frame on the window's thread hands it to the engine: look as mouse movement (`bo1_native_input_motion`, 1/256 pixel kept between frames), the joystick into pad slot 0, keys through the key path (`bo1_native_input_keycode`), commands into `Cbuf_AddText` |
| `src/native/native_gamepad.cpp` | calls it with slot 0's state before publishing it |
| `src/web/web_input.cpp` | `INPUT_KEYCODE`: an engine key code as a key event (`SE_KEY`), so the AUX and keypad keys run their binds |
| `src/client_mp/cl_touch.cpp`, `cl_extras.cpp` | `bo1_second_screen` and the EXTRAS row |

A touch costs one JNI call. Nothing is drawn or computed between touches, except the dim timer.

## Not done

- A pointer for menus on the pad (it turns the front end's view instead; menus use the controller).
- Gyro, per-game profiles beyond Zombies / Multiplayer, naming presets (no keyboard on a window without focus).
- Remapping the sticks (`bindaxis`): the pad's look is not an axis the engine binds.

## Limits

- While the controller is visible and no controller is connected, pad slot 0 counts as connected: `gpad_enabled`
  follows it, and prompts show pad buttons.
- Touching the launcher while the controller is hidden moves the system's focus there; the game gets it back with a
  touch on the main display.
- Look sensitivity and the press depth are starting values; the Thor's best values are not settled yet.
- The size press needs a panel that reports contact size. One that reports a constant size never presses; use the
  double tap or the hold there.

## Test plan (adb)

The Thor's displays: main `screencap -d 4630946441858561667`, second `screencap -d 4630946482288158084`; input on the
second display: `input -d 4`. Coordinates below are for the Thor's 1240x1080 at rotation 1; take them from the
layout line in `bo1.log` for another shape.

| target | tap point |
| --- | --- |
| HIDE | 158 43 |
| REMAP | 1081 43 |
| PADS (when hidden) | top right corner, about 1160 40 |
| pad centre | 620 578 |

1. Start the game; read `bo1.log` for the lines under "Start it"; screenshot the second display.
2. In a map, look: `input -d 4 swipe 450 578 800 578 300`. The view turns right.
3. Size press, injected as raw touches on the second screen's panel (root; `fts_ts_3` is `/dev/input/event5` on the
   Thor; codes: `EV_ABS` 3, `ABS_MT_SLOT` 47, `ABS_MT_TOUCH_MAJOR` 48, `ABS_MT_POSITION_X` 53, `_Y` 54,
   `ABS_MT_TRACKING_ID` 57, `EV_KEY` 1 `BTN_TOUCH` 330, `SYN_REPORT` 0 0 0). Touch down at size 4, then within 0.3 s
   raise it 6, 8, 10, 12, hold, then lower it 8, 5, 4 and lift (tracking id 4294967295). `bo1.log`:
   `second screen: pad down: size 4.0`, `second screen: press: size 10.0 relaxed 4.0 threshold 9.0`,
   `second screen: press released: size 5.0`. In a map the weapon fires between those two lines.
4. Other click methods: REMAP > PADS > Pad click > Press and hold, then `input -d 4 swipe 620 578 620 578 700`
   (held still 700 ms) fires while held. Test the double tap with a finger.
5. REMAP > BUTTONS > Trackpad press > Melee (knife). Then
   `adb shell "echo bindlist > /sdcard/Android/data/org.bo1port.zombies.turnip/files/cmd.txt"` and
   `adb shell grep -E "AUX|BUTTON_|DPAD_" /sdcard/Android/data/org.bo1port.zombies.turnip/files/bo1.log | tail -40`.
   `AUX6` shows `+melee`.
6. PRESETS > Reset to console default; `bindlist` again: `AUX6 "+attack"`, `BUTTON_A "+gostand"`.
7. EXTRAS > SECOND-SCREEN CONTROLS Off: the launcher is back on the second display. On: the controller returns.
8. Focus: keep a finger on the pad and use the built-in controller: both work; the game keeps running at its frame
   rate.
