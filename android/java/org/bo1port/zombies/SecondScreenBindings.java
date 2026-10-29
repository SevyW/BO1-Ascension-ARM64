package org.bo1port.zombies;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What can be remapped and to what, without Android classes (tested on the build host). Inputs are engine key
 * names: the built-in pad's buttons (BUTTON_A ... DPAD_RIGHT, docs/controllers.md) and the second screen's own
 * inputs, which are keys nothing else uses on Android (src/native/native_second_screen.cpp): AUX1..AUX12 and the
 * keypad keys. A bind is applied as the console command "bind KEY \"command\"", which the engine saves in
 * players/config.cfg (config_mp.cfg in Multiplayer).
 */
final class SecondScreenBindings {
    private SecondScreenBindings() {
    }

    static final class Input {
        final String key;     // the engine's key name
        final String label;   // what the remapping screen calls it
        final int code;       // the engine key code the second screen sends (0: a physical button, not sent from here)

        Input(String key, String label, int code) {
            this.key = key;
            this.label = label;
            this.code = code;
        }
    }

    static final class Action {
        final String command;   // "" = unbound
        final String label;

        Action(String command, String label) {
            this.command = command;
            this.label = label;
        }
    }

    // the second screen's inputs (key codes: src/client/cl_keys.cpp keynames)
    static final Input L4 = new Input("AUX1", "L4 grip", 207);
    static final Input R4 = new Input("AUX2", "R4 grip", 208);
    static final Input L5 = new Input("AUX3", "L5 grip", 209);
    static final Input R5 = new Input("AUX4", "R5 grip", 210);
    static final Input LEFT_CLICK = new Input("AUX5", "Left pad click", 211);
    static final Input RIGHT_CLICK = new Input("AUX6", "Right pad click", 212);
    static final Input[] LEFT_DPAD = {
        new Input("AUX7", "Left pad d-pad up", 213),
        new Input("AUX8", "Left pad d-pad down", 214),
        new Input("AUX9", "Left pad d-pad left", 215),
        new Input("AUX10", "Left pad d-pad right", 216),
    };
    static final Input SCROLL_NEXT = new Input("AUX11", "Scroll wheel clockwise", 217);
    static final Input SCROLL_PREVIOUS = new Input("AUX12", "Scroll wheel counter-clockwise", 218);
    static final Input[] RADIAL = {
        new Input("KP_HOME", "Radial slice 1 (top)", 182),
        new Input("KP_UPARROW", "Radial slice 2", 183),
        new Input("KP_PGUP", "Radial slice 3", 184),
        new Input("KP_LEFTARROW", "Radial slice 4", 185),
        new Input("KP_5", "Radial slice 5", 186),
        new Input("KP_RIGHTARROW", "Radial slice 6", 187),
        new Input("KP_END", "Radial slice 7", 188),
        new Input("KP_DOWNARROW", "Radial slice 8", 189),
    };

    static final Input[] PHYSICAL = {
        new Input("BUTTON_A", "A", 0),
        new Input("BUTTON_B", "B", 0),
        new Input("BUTTON_X", "X", 0),
        new Input("BUTTON_Y", "Y", 0),
        new Input("BUTTON_LSHLDR", "LB", 0),
        new Input("BUTTON_RSHLDR", "RB", 0),
        new Input("BUTTON_LTRIG", "LT", 0),
        new Input("BUTTON_RTRIG", "RT", 0),
        new Input("BUTTON_LSTICK", "L3 (left stick click)", 0),
        new Input("BUTTON_RSTICK", "R3 (right stick click)", 0),
        new Input("BUTTON_START", "Start", 0),
        new Input("BUTTON_BACK", "Select", 0),
        new Input("DPAD_UP", "D-pad up", 0),
        new Input("DPAD_DOWN", "D-pad down", 0),
        new Input("DPAD_LEFT", "D-pad left", 0),
        new Input("DPAD_RIGHT", "D-pad right", 0),
    };

    /** Every remappable input, in the order the remapping screen lists them. */
    static List<Input> all() {
        List<Input> out = new ArrayList<Input>();
        for (Input i : PHYSICAL) {
            out.add(i);
        }
        out.add(L4);
        out.add(R4);
        out.add(L5);
        out.add(R5);
        out.add(LEFT_CLICK);
        out.add(RIGHT_CLICK);
        for (Input i : LEFT_DPAD) {
            out.add(i);
        }
        out.add(SCROLL_NEXT);
        out.add(SCROLL_PREVIOUS);
        for (Input i : RADIAL) {
            out.add(i);
        }
        return out;
    }

    /** The second screen's own inputs only. */
    static List<Input> virtual() {
        List<Input> out = new ArrayList<Input>();
        for (Input i : all()) {
            if (i.code != 0) {
                out.add(i);
            }
        }
        return out;
    }

    /** Engine commands (client_mp/cl_input_mp.cpp, cgame), named for the player. */
    static final Action[] ACTIONS = {
        new Action("", "Nothing (unbound)"),
        new Action("+attack", "Fire"),
        new Action("+speed_throw", "Aim down sight (hold)"),
        new Action("+toggleads_throw", "Aim down sight (toggle)"),
        new Action("+gostand", "Jump / stand up"),
        new Action("+stance", "Crouch (tap) / prone (hold)"),
        new Action("togglecrouch", "Crouch (toggle)"),
        new Action("toggleprone", "Prone (toggle)"),
        new Action("+usereload", "Reload (tap) / use (hold)"),
        new Action("+reload", "Reload"),
        new Action("+activate", "Use (buy, revive, doors)"),
        new Action("+melee", "Melee (knife)"),
        new Action("+breath_sprint", "Sprint / hold breath"),
        new Action("+sprint", "Sprint"),
        new Action("+frag", "Lethal grenade"),
        new Action("+smoke", "Tactical grenade"),
        new Action("weapnext", "Next weapon"),
        new Action("weapprev", "Previous weapon"),
        new Action("+actionslot 1", "Action slot 1 (d-pad up)"),
        new Action("+actionslot 2", "Action slot 2 (d-pad down)"),
        new Action("+actionslot 3", "Action slot 3 (d-pad left)"),
        new Action("+actionslot 4", "Action slot 4 (d-pad right)"),
        new Action("+leanleft", "Lean left"),
        new Action("+leanright", "Lean right"),
        new Action("+scores", "Scoreboard (hold)"),
        new Action("togglemenu", "Pause menu"),
    };

    /** The readable name of a bound command (the command itself when it is not in the list). */
    static String label(String command) {
        if (command == null || command.length() == 0) {
            return ACTIONS[0].label;
        }
        for (Action a : ACTIONS) {
            if (a.command.equalsIgnoreCase(command)) {
                return a.label;
            }
        }
        return command;
    }

    /**
     * The console default: the BO1 console "Default" pad layout (client/cl_gamepad.cpp s_gpadDefaultButtonBinds)
     * and the second screen's inputs: jump / crouch on the upper grips, knife / use-reload on the lower ones, sprint
     * and fire on the pad clicks, the action slots on the d-pad zones, weapons on the scroll wheel.
     */
    static Map<String, String> consoleDefault() {
        Map<String, String> m = new LinkedHashMap<String, String>();
        m.put("BUTTON_A", "+gostand");
        m.put("BUTTON_B", "+stance");
        m.put("BUTTON_X", "+usereload");
        m.put("BUTTON_Y", "weapnext");
        m.put("BUTTON_LSHLDR", "+smoke");
        m.put("BUTTON_RSHLDR", "+frag");
        m.put("BUTTON_LTRIG", "+speed_throw");
        m.put("BUTTON_RTRIG", "+attack");
        m.put("BUTTON_LSTICK", "+breath_sprint");
        m.put("BUTTON_RSTICK", "+melee");
        m.put("BUTTON_START", "togglemenu");
        m.put("BUTTON_BACK", "+scores");
        m.put("DPAD_UP", "+actionslot 1");
        m.put("DPAD_DOWN", "+actionslot 2");
        m.put("DPAD_LEFT", "+actionslot 3");
        m.put("DPAD_RIGHT", "+actionslot 4");
        m.put(L4.key, "+gostand");
        m.put(R4.key, "+stance");
        m.put(L5.key, "+melee");
        m.put(R5.key, "+usereload");
        m.put(LEFT_CLICK.key, "+breath_sprint");
        m.put(RIGHT_CLICK.key, "+attack");
        for (int i = 0; i < 4; ++i) {
            m.put(LEFT_DPAD[i].key, "+actionslot " + (i + 1));
        }
        m.put(SCROLL_NEXT.key, "weapnext");
        m.put(SCROLL_PREVIOUS.key, "weapprev");
        final String[] radial = {"+actionslot 1", "+actionslot 4", "+frag", "+actionslot 2", "+smoke", "+actionslot 3",
            "+scores", "weapnext"};
        for (int i = 0; i < RADIAL.length; ++i) {
            m.put(RADIAL[i].key, radial[i]);
        }
        return m;
    }

    /** The console command that gives key that command ("" unbinds it). One command, no separators. */
    static String command(String key, String command) {
        if (command == null || command.length() == 0) {
            return "unbind " + key;
        }
        return "bind " + key + " \"" + command.replace("\"", "").replace(";", "") + "\"";
    }

    /** "KEY=binding" lines (what the engine sends back) as a map. */
    static Map<String, String> parse(String text) {
        Map<String, String> m = new LinkedHashMap<String, String>();
        if (text == null) {
            return m;
        }
        for (String line : text.split("\n")) {
            final int eq = line.indexOf('=');
            if (eq > 0) {
                m.put(line.substring(0, eq), line.substring(eq + 1));
            }
        }
        return m;
    }

    /** The key names of a list of inputs, space separated (what the engine is asked for). */
    static String keyList(List<Input> inputs) {
        StringBuilder b = new StringBuilder();
        for (Input i : inputs) {
            if (b.length() > 0) {
                b.append(' ');
            }
            b.append(i.key);
        }
        return b.toString();
    }
}
