# Second-screen controls (NOT part of the original game)

On a dual-screen Android device the second display becomes a controller in the style of the Steam Deck: two
trackpads, four grip buttons, and a screen for remapping every button while the game runs on the main display. Made
for the AYN Thor (lower screen: display 4, "Screen-2", 1080x1240, 290 dpi, `FLAG_PRESENTATION`). On a device with one
display nothing changes.

Status (2026-10-29): built, the maths unit-tested on the build host; not yet run on a device.

## Start it

Start the game. If the device has a display meant for presentations (`Display.FLAG_PRESENTATION`) other than the
game's, the controller appears on it by itself and covers the app that was there (the Thor's launcher). Settings >
EXTRAS > SECOND-SCREEN CONTROLS turns it off and on (`bo1_second_screen`, below).

`files/bo1.log` shows, in this order (the Java lines are held until the engine is running, then written together):

```
second screen: natives registered
second screen: layout 1080x1240 leftPad 141,476-529,864 rightPad 551,476-939,864 L4 22,121-119,659 L5 22,681-119,1218 R4 961,121-1058,659 R5 961,681-1058,1218 HIDE 22,11-260,88 REMAP 820,11-1058,88
second screen: controller on display 4 "Screen-2" 1080x1240 rotation 0 290 dpi
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

| control | does | default |
| --- | --- | --- |
| right pad | turns the view as a mouse does (the engine's mouse path). Sensitivity, acceleration, trackball inertia | look |
| right pad click | key `AUX6` | `+attack` (fire) |
| left pad | one of five modes, below | d-pad |
| left pad click | key `AUX5` (d-pad mode: the zone under the finger instead) | `+breath_sprint` (sprint) |
| L4, R4 (upper grips) | keys `AUX1`, `AUX2` | `+gostand` (jump), `+stance` (crouch / prone) |
| L5, R5 (lower grips) | keys `AUX3`, `AUX4` | `+melee` (knife), `+usereload` (reload / use) |

A pad click is a double tap (the second touch within 300 ms of the first, near it) or a finger held still for the
hold time; it lasts until that finger lifts. A haptic click comes with each click and grip press, a lighter tick when
a finger enters a d-pad zone, a radial slice or a scroll step.

Left pad modes:

| mode | does | keys | default binds |
| --- | --- | --- | --- |
| D-pad | four zones (up, down, left, right, split along the diagonals; none in the middle 12 %); a click presses the zone under the finger | `AUX7`-`AUX10` | `+actionslot 1`-`4` |
| Joystick | the finger's offset from where it touched down is the left stick: dead zone 12 %, full deflection at 40 % of the pad's size | the pad's left stick | move |
| Radial | 4, 6 or 8 slices, slice 1 at the top, then clockwise; lifting the finger on a slice presses its key | `KP_HOME`, `KP_UPARROW`, `KP_PGUP`, `KP_LEFTARROW`, `KP_5`, `KP_RIGHTARROW`, `KP_END`, `KP_DOWNARROW` | `+actionslot 1`, `+actionslot 4`, `+frag`, `+actionslot 2`, `+smoke`, `+actionslot 3`, `+scores`, `weapnext` |
| Scroll | circling the middle steps every 30 degrees: clockwise `AUX11`, counter-clockwise `AUX12` | `AUX11`, `AUX12` | `weapnext`, `weapprev` |
| Off | nothing | | |

The second screen's keys are keys nothing else uses on Android, bound like any key (`bind AUX1 "+gostand"`) and saved
with the other binds in `files/home/players/config.cfg` (`config_mp.cfg` in Multiplayer).

In menus the right pad still sends mouse movement but not a pointer position: in the front end's room with the TV
menus it turns the view as a mouse does; menus with a pointer ignore it. Menus are driven by the controller or a tap
on the main display, as before.

## Remapping

REMAP (top right) opens three tabs:

- BUTTONS: every input (A, B, X, Y, LB, RB, LT, RT, L3, R3, Start, Select, the d-pad, the four grips, both pad
  clicks, the d-pad zones, the scroll steps and the radial slices in use) with its action. A tap opens the list of
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
| trackball: a flick keeps turning | on | on / off |
| trackball friction: the speed falls by `exp(-friction * seconds)`; a flick of v px/s coasts about v / friction px | 4 /s | 0.5-10 |
| left pad mode | d-pad | d-pad, joystick, radial, scroll, off |
| radial slices | 8 | 4, 6, 8 |
| joystick dead zone | 12 % | 0-50 % |
| pad click | double tap | double tap, press and hold |
| hold time | 350 ms | 150-800 ms |
| haptics | on | on / off |
| dim after (the controller fades to 30 % until the next touch) | 5 s | 0 (never)-30 s |

| dvar | default | range | |
| --- | --- | --- | --- |
| `bo1_second_screen` | 1 | 0 / 1, saved | the controller on the second display (Settings > EXTRAS) |

`controls.json` also records which modes already got the default binds (`defaultsApplied`) and the saved presets.

## How it works

| file | what |
| --- | --- |
| `android/java/org/bo1port/zombies/SecondScreen.java` | picks the display (`DisplayManager.getDisplays(DISPLAY_CATEGORY_PRESENTATION)`, the first with `FLAG_PRESENTATION` that is not the game's), shows a `Presentation` there with `FLAG_NOT_FOCUSABLE` and `FLAG_NOT_TOUCH_MODAL`, follows displays coming and going, the activity's onStart / onStop, the engine's state; haptics; default binds |
| `SecondScreenView.java` | draws the pads and grips and follows fingers by pointer id; redraws only on a change |
| `SecondScreenRemap.java` | the remapping screen |
| `SecondScreenMath.java`, `SecondScreenBindings.java` | layout, gesture maths, inputs, actions and presets, without Android classes (tested on the build host: `tools/native/test-second-screen.sh`) |
| `SecondScreenSettings.java` | `files/controls.json` |
| `Bo1Activity.java` | the `ss*` native methods, `secondScreenState` / `secondScreenBinds` from the engine, onStart / onStop |
| `src/native/native_second_screen.cpp` | registers the native methods on the activity's class (libbo1.so is not loaded by the Java class loader), keeps what the JNI calls carry, and once per frame on the window's thread hands it to the engine: look as mouse movement (`bo1_native_input_motion`, 1/256 pixel kept between frames), the joystick into pad slot 0, keys through the key path (`bo1_native_input_keycode`), commands into `Cbuf_AddText` |
| `src/native/native_gamepad.cpp` | calls it with slot 0's state before publishing it |
| `src/web/web_input.cpp` | `INPUT_KEYCODE`: an engine key code as a key event (`SE_KEY`), so the AUX and keypad keys run their binds |
| `src/client_mp/cl_touch.cpp`, `cl_extras.cpp` | `bo1_second_screen` and the EXTRAS row |

A touch costs one JNI call. Nothing is drawn or computed between touches, except the trackball's per-frame steps
while a flick coasts and the dim timer.

## Not done

- A pointer for menus on the right pad (the right pad turns the front end's view instead; menus use the controller).
- Gyro, per-game profiles beyond Zombies / Multiplayer, naming presets (no keyboard on a window without focus).
- Remapping the sticks (`bindaxis`): the pads' look and joystick are not axes the engine binds.

## Limits

- While the controller is visible and no controller is connected, pad slot 0 counts as connected (so the left pad's
  joystick moves the player): `gpad_enabled` follows it, and prompts show pad buttons.
- Radial slices use keypad keys: in a menu they act as the keypad's arrows and Enter would (the radial slice 5 key is
  `KP_5`).
- Touching the launcher while the controller is hidden moves the system's focus there; the game gets it back with a
  touch on the main display.
- Look sensitivity is a starting value; the Thor's best value is not measured yet.

## Test plan (adb)

The Thor's displays: main `screencap -d 4630946441858561667`, second `screencap -d 4630946482288158084`; input on the
second display: `input -d 4`. Coordinates below are for 1080x1240 at rotation 0; take them from the layout line
in `bo1.log` for another orientation.

| target | tap point |
| --- | --- |
| HIDE | 141 50 |
| REMAP | 939 50 |
| PADS (when hidden) | top right corner, about 1000 40 |
| left pad centre | 335 670 |
| right pad centre | 745 670 |
| L4 / L5 / R4 / R5 | 70 390 / 70 950 / 1010 390 / 1010 950 |

1. Start the game; read `bo1.log` for the lines under "Start it"; screenshot the second display.
2. In a map, look: `input -d 4 swipe 650 670 850 670 300`. The view turns right. A fast swipe
   (`input -d 4 swipe 650 670 850 670 40`) keeps turning after it ends (trackball).
3. Grips: `input -d 4 swipe 70 390 70 390 300` (a 300 ms press of L4): the player jumps.
4. Clicks over adb: each `input` command starts its own process, which often takes longer than the 300 ms of a
   double tap, so set REMAP > PADS > Pad click > Press and hold first (and back afterwards; test the double tap with
   a finger). Right pad click: `input -d 4 swipe 745 670 745 670 700` (held still 700 ms): fires while held.
5. D-pad zone, hold method: `input -d 4 swipe 335 520 335 520 700` (high on the left pad): action slot 1
   (`+actionslot 1`).
6. REMAP > BUTTONS > L4 > Melee (knife). Then
   `adb shell "echo bindlist > /sdcard/Android/data/org.bo1port.zombies.turnip/files/cmd.txt"` and
   `adb shell grep -E "AUX|KP_|BUTTON_|DPAD_" /sdcard/Android/data/org.bo1port.zombies.turnip/files/bo1.log | tail -40`.
   `AUX1` shows `+melee`. After leaving the game: `adb shell grep AUX1
   /sdcard/Android/data/org.bo1port.zombies.turnip/files/home/players/config.cfg`.
7. PRESETS > Reset to console default; `bindlist` again: `AUX1 "+gostand"`.
8. EXTRAS > SECOND-SCREEN CONTROLS Off: the launcher is back on the second display. On: the controller returns.
9. Focus: hold a grip with one finger and use the built-in controller: both work; the game keeps running at its
   frame rate.
