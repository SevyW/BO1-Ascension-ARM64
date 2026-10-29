package org.bo1port.zombies;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The second-screen controller's settings, in files/controls.json (the app's external files folder, next to
 * bo1.log). Binds are not kept here: they are the engine's (players/config.cfg). Saved presets are: a preset is
 * a key name -> command map, one set of slots for Zombies ("zm") and one for Multiplayer ("mp").
 */
final class SecondScreenSettings {
    static final int PRESET_SLOTS = 3;

    float sensitivity = 1.0f;     // look per finger pixel, 0.1 .. 5
    float acceleration = 0.5f;    // extra look for fast swipes, 0 .. 2
    int clickMethod = SecondScreenMath.Click.PRESS;
    int holdMs = 350;             // HOLD: 150 .. 800
    float pressDepth = 5f;        // PRESS: how much the finger's contact size must grow (raw units), 3 .. 10
    boolean haptics = true;
    int clickFeel = 1;            // SecondScreen.FEELS, 0 .. 9
    int dimSeconds = 5;           // 0 = never
    final Map<String, Boolean> defaultsApplied = new LinkedHashMap<String, Boolean>();
    final Map<String, Map<String, String>> presets = new LinkedHashMap<String, Map<String, String>>();

    private final File file;

    SecondScreenSettings(File folder) {
        file = new File(folder, "controls.json");
    }

    static String presetName(String mode, int slot) {
        return mode + "." + slot;
    }

    void load() {
        if (!file.exists()) {
            return;
        }
        try {
            FileInputStream in = new FileInputStream(file);
            byte[] bytes = new byte[(int) Math.min(file.length(), 1 << 20)];
            int n = 0;
            while (n < bytes.length) {
                int r = in.read(bytes, n, bytes.length - n);
                if (r < 0) {
                    break;
                }
                n += r;
            }
            in.close();
            JSONObject o = new JSONObject(new String(bytes, 0, n, "UTF-8"));
            sensitivity = clamp((float) o.optDouble("sensitivity", sensitivity), 0.1f, 5f);
            acceleration = clamp((float) o.optDouble("acceleration", acceleration), 0f, 2f);
            // "click" (press / doubletap / hold); the older "clickMethod" key is ignored, so the size press
            // becomes the default for files written before it existed
            final String click = o.optString("click", "press");
            clickMethod = "hold".equals(click) ? SecondScreenMath.Click.HOLD
                : "doubletap".equals(click) ? SecondScreenMath.Click.DOUBLE_TAP : SecondScreenMath.Click.PRESS;
            holdMs = (int) clamp(o.optInt("holdMs", holdMs), 150, 800);
            pressDepth = clamp((float) o.optDouble("pressDepth", pressDepth), 3f, 10f);
            haptics = o.optBoolean("haptics", haptics);
            clickFeel = (int) clamp(o.optInt("clickFeel", clickFeel), 0, SecondScreen.FEELS.length - 1);
            dimSeconds = (int) clamp(o.optInt("dimSeconds", dimSeconds), 0, 60);
            JSONObject applied = o.optJSONObject("defaultsApplied");
            if (applied != null) {
                for (Iterator<String> it = applied.keys(); it.hasNext();) {
                    String k = it.next();
                    defaultsApplied.put(k, applied.optBoolean(k));
                }
            }
            JSONObject p = o.optJSONObject("presets");
            if (p != null) {
                for (Iterator<String> it = p.keys(); it.hasNext();) {
                    String name = it.next();
                    JSONObject binds = p.optJSONObject(name);
                    if (binds == null) {
                        continue;
                    }
                    Map<String, String> m = new LinkedHashMap<String, String>();
                    for (Iterator<String> k = binds.keys(); k.hasNext();) {
                        String key = k.next();
                        m.put(key, binds.optString(key, ""));
                    }
                    presets.put(name, m);
                }
            }
        } catch (Exception e) {
            SecondScreen.log("controls.json not read: " + e);
        }
    }

    void save() {
        try {
            JSONObject o = new JSONObject();
            o.put("sensitivity", sensitivity);
            o.put("acceleration", acceleration);
            o.put("click", clickMethod == SecondScreenMath.Click.HOLD ? "hold"
                : clickMethod == SecondScreenMath.Click.DOUBLE_TAP ? "doubletap" : "press");
            o.put("holdMs", holdMs);
            o.put("pressDepth", pressDepth);
            o.put("haptics", haptics);
            o.put("clickFeel", clickFeel);
            o.put("dimSeconds", dimSeconds);
            JSONObject applied = new JSONObject();
            for (Map.Entry<String, Boolean> e : defaultsApplied.entrySet()) {
                applied.put(e.getKey(), e.getValue());
            }
            o.put("defaultsApplied", applied);
            JSONObject p = new JSONObject();
            for (Map.Entry<String, Map<String, String>> e : presets.entrySet()) {
                p.put(e.getKey(), new JSONObject(e.getValue()));
            }
            o.put("presets", p);
            File temp = new File(file.getPath() + ".tmp");
            FileOutputStream out = new FileOutputStream(temp);
            out.write(o.toString(2).getBytes("UTF-8"));
            out.close();
            if (!temp.renameTo(file)) {
                SecondScreen.log("controls.json not saved");
            }
        } catch (Exception e) {
            SecondScreen.log("controls.json not saved: " + e);
        }
    }

    private static float clamp(float v, float low, float high) {
        return v < low ? low : v > high ? high : v;
    }
}
