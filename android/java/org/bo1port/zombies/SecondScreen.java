package org.bo1port.zombies;

import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The second-screen controller (docs/second-screen.md): on a device with a second display that is meant for
 * presentations (FLAG_PRESENTATION, e.g. the AYN Thor's lower screen), a Presentation there shows two trackpads
 * and four grip buttons (SecondScreenView) and the remapping screen (SecondScreenRemap). On a device without one
 * nothing is created.
 *
 * What the pads do reaches the engine through Bo1Activity's static native methods, which libbo1.so registers once
 * it runs (src/native/native_second_screen.cpp); until the engine's first secondScreenState call nothing is sent.
 * The window never takes focus, so the game on the main display keeps the controller and the keyboard while the
 * pads are touched. Settings > EXTRAS > SECOND-SCREEN CONTROLS (bo1_second_screen) turns it off and on; the bar's
 * HIDE shrinks it to a small PADS button so the display's own app can be used.
 */
final class SecondScreen implements DisplayManager.DisplayListener {
    private static SecondScreen instance;
    private static final List<String> early = new ArrayList<String>();   // log lines from before the engine is there

    private final Bo1Activity activity;
    final SecondScreenSettings settings;
    private final DisplayManager displays;
    private final Vibrator vibrator;
    private final Handler main = new Handler(Looper.getMainLooper());
    private Pads pads;
    private boolean started;           // between the activity's onStart and onStop
    private volatile boolean ready;    // the engine registered the natives
    private int enabled = 1;           // bo1_second_screen
    private int ingame = -1;
    private boolean zombies = true;
    private boolean hidden;            // HIDE: only the PADS button shows
    private boolean remapping;
    private boolean defaultsChecked;   // this start's check whether the second screen's keys need their default binds

    SecondScreen(Bo1Activity activity) {
        instance = this;
        this.activity = activity;
        File folder = activity.getExternalFilesDir(null);
        settings = new SecondScreenSettings(folder != null ? folder : activity.getFilesDir());
        settings.load();
        displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        vibrator = (Vibrator) activity.getSystemService(Context.VIBRATOR_SERVICE);
    }

    // ---- to the engine (Bo1Activity's natives; any thread) --------------------------------------------------------
    static void log(String text) {
        Log.i("bo1", "second screen: " + text);
        SecondScreen s = instance;
        if (s != null && s.ready) {
            try {
                Bo1Activity.ssLog(text);
                return;
            } catch (Throwable ignored) {
                // logcat has it
            }
        }
        synchronized (early) {
            if (early.size() < 50) {
                early.add(text);
            }
        }
    }

    void look(float dx, float dy) {
        if (ready) {
            Bo1Activity.ssLook(dx, dy);
        }
    }

    void stick(float x, float y) {
        if (ready) {
            Bo1Activity.ssStick(x, y);
        }
    }

    void key(int code, boolean down) {
        if (ready && code > 0) {
            Bo1Activity.ssKey(code, down);
        }
    }

    /** A press and, 80 ms later, the release (radial slices, scroll steps). */
    void tapKey(final int code) {
        key(code, true);
        main.postDelayed(new Runnable() {
            @Override
            public void run() {
                key(code, false);
            }
        }, 80);
    }

    void bind(String key, String command) {
        if (ready) {
            Bo1Activity.ssCommand(SecondScreenBindings.command(key, command));
            requestBindsSoon();
        }
    }

    void applyPreset(Map<String, String> binds) {
        if (!ready) {
            return;
        }
        for (Map.Entry<String, String> e : binds.entrySet()) {
            Bo1Activity.ssCommand(SecondScreenBindings.command(e.getKey(), e.getValue()));
        }
        log("preset applied: " + binds.size() + " binds");
        requestBindsSoon();
    }

    void requestBinds() {
        if (ready) {
            Bo1Activity.ssRequestBinds(SecondScreenBindings.keyList(SecondScreenBindings.all()));
        }
    }

    private void requestBindsSoon() {
        main.postDelayed(new Runnable() {
            @Override
            public void run() {
                requestBinds();
            }
        }, 300);
    }

    /** The click feels the PADS tab offers (settings.clickFeel), played by feel(). */
    static final String[] FEELS = {"Soft tick", "Light click", "Crisp click", "Low tick (deep, quiet)", "Soft thud",
        "Double tick", "Click, and a tick on release", "Trigger pull (rise, then click)", "Heavy click", "Short buzz"};

    /** The pad's click (press true) or its release, as the chosen feel; only "Click, and a tick on release" marks the release. */
    void feel(boolean press) {
        if (!settings.haptics || vibrator == null) {
            return;
        }
        playFeel(settings.clickFeel, press);
    }

    /** Plays feel number f (FEELS), for trying them on the PADS tab too. */
    void playFeel(int f, boolean press) {
        if (vibrator == null) {
            return;
        }
        try {
            if (Build.VERSION.SDK_INT < 30) {
                if (press) {
                    vibrator.vibrate(VibrationEffect.createOneShot(f == 8 ? 30 : f == 9 ? 15 : 10, VibrationEffect.DEFAULT_AMPLITUDE));
                }
                return;
            }
            final boolean deep = Build.VERSION.SDK_INT >= 31;   // LOW_TICK and THUD
            VibrationEffect.Composition c = VibrationEffect.startComposition();
            if (!press) {
                if (f != 6) {
                    return;
                }
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 0.5f);
                vibrator.vibrate(c.compose());
                return;
            }
            switch (f) {
                case 0:
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 0.5f);
                    break;
                case 2:
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 1f);
                    break;
                case 3:
                    c.addPrimitive(deep ? VibrationEffect.Composition.PRIMITIVE_LOW_TICK : VibrationEffect.Composition.PRIMITIVE_TICK, 1f);
                    break;
                case 4:
                    c.addPrimitive(deep ? VibrationEffect.Composition.PRIMITIVE_THUD : VibrationEffect.Composition.PRIMITIVE_CLICK, 0.5f);
                    break;
                case 5:
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 0.8f);
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 0.8f, 50);
                    break;
                case 6:
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 0.6f);
                    break;
                case 7:
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_QUICK_RISE, 0.5f);
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 0.8f);
                    break;
                case 8:
                    vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK));
                    return;
                case 9:
                    vibrator.vibrate(VibrationEffect.createOneShot(15, 110));
                    return;
                default:   // 1, light click
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 0.5f);
                    break;
            }
            vibrator.vibrate(c.compose());
        } catch (Throwable ignored) {
            // no vibrator, or a primitive it can't play
        }
    }

    void haptic(boolean strong) {
        if (!settings.haptics || vibrator == null) {
            return;
        }
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                vibrator.vibrate(VibrationEffect.createPredefined(strong ? VibrationEffect.EFFECT_CLICK : VibrationEffect.EFFECT_TICK));
            } else {
                vibrator.vibrate(VibrationEffect.createOneShot(strong ? 20 : 8, VibrationEffect.DEFAULT_AMPLITUDE));
            }
        } catch (Throwable ignored) {
            // no vibrator
        }
    }

    // ---- from the engine (Bo1Activity.secondScreenState / secondScreenBinds, on the engine's thread) ---------------
    /** "<bo1_second_screen> <in a game> <Zombies>", each 0 / 1 (-1: not known yet). */
    void onState(final String state) {
        main.post(new Runnable() {
            @Override
            public void run() {
                String[] f = state.trim().split(" ");
                int e = f.length > 0 ? parse(f[0]) : -1, g = f.length > 1 ? parse(f[1]) : -1, z = f.length > 2 ? parse(f[2]) : 1;
                if (!ready) {
                    ready = true;
                    List<String> lines;
                    synchronized (early) {
                        lines = new ArrayList<String>(early);
                        early.clear();
                    }
                    for (String line : lines) {
                        log(line);
                    }
                    log("engine connected");
                }
                final boolean modeChanged = z >= 0 && (z != 0) != zombies;
                if (z >= 0) {
                    zombies = z != 0;
                }
                ingame = g;
                final int wanted = e == 0 ? 0 : 1;
                if (wanted != enabled) {
                    enabled = wanted;
                    log("bo1_second_screen " + enabled);
                }
                if (z >= 0 && (!defaultsChecked || modeChanged)) {
                    defaultsChecked = true;
                    requestBinds();
                }
                update();
                if (pads != null) {
                    pads.refresh();
                }
            }
        });
    }

    void onBinds(final String text) {
        main.post(new Runnable() {
            @Override
            public void run() {
                Map<String, String> binds = SecondScreenBindings.parse(text);
                final String mode = zombies ? "zm" : "mp";
                Boolean done = settings.defaultsApplied.get(mode);
                if (done == null || !done) {
                    // the first start in this mode: the second screen's keys get their defaults, unless the player
                    // already bound one of them
                    boolean anyBound = false;
                    for (SecondScreenBindings.Input i : SecondScreenBindings.virtual()) {
                        String b = binds.get(i.key);
                        anyBound |= b != null && b.length() > 0;
                    }
                    if (!anyBound && binds.size() > 0) {
                        Map<String, String> defaults = SecondScreenBindings.consoleDefault();
                        for (SecondScreenBindings.Input i : SecondScreenBindings.virtual()) {
                            Bo1Activity.ssCommand(SecondScreenBindings.command(i.key, defaults.get(i.key)));
                            binds.put(i.key, defaults.get(i.key));
                        }
                        log("default binds for the second screen's keys (" + mode + ")");
                    }
                    if (binds.size() > 0) {
                        settings.defaultsApplied.put(mode, true);
                        settings.save();
                    }
                }
                if (pads != null && pads.remap != null) {
                    pads.remap.setBinds(binds);
                }
            }
        });
    }

    private static int parse(String s) {
        try {
            return Integer.parseInt(s);
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    boolean zombies() {
        return zombies;
    }

    String stateText() {
        return (zombies ? "ZOMBIES" : "MULTIPLAYER") + (ingame == 1 ? " - IN GAME" : ingame == 0 ? " - MENU" : "");
    }

    // ---- the activity's lifecycle -------------------------------------------------------------------------------
    void start() {
        started = true;
        displays.registerDisplayListener(this, main);
        main.post(new Runnable() {
            @Override
            public void run() {
                update();
            }
        });
    }

    void stop() {
        started = false;
        displays.unregisterDisplayListener(this);
        dismiss();
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (pads != null && pads.getDisplay().getDisplayId() == displayId) {
            dismiss();
        }
        update();
    }

    @Override
    public void onDisplayChanged(int displayId) {
        update();
    }

    /** The first display for presentations that is not the game's. */
    private Display pick() {
        final int own = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        for (Display d : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (d.getDisplayId() != own && (d.getFlags() & Display.FLAG_PRESENTATION) != 0) {
                return d;
            }
        }
        return null;
    }

    /** Shows or removes the controller for the current state. */
    void update() {
        if (pads != null && !pads.isShowing()) {
            pads = null;   // dismissed by the system (display changed or removed)
        }
        final Display display = started && enabled != 0 ? pick() : null;
        if (display == null) {
            if (pads != null) {
                dismiss();
            }
            return;
        }
        if (pads != null && pads.getDisplay().getDisplayId() == display.getDisplayId()) {
            return;
        }
        dismiss();
        try {
            pads = new Pads(activity, display);
            pads.show();
            DisplayMetrics m = new DisplayMetrics();
            display.getRealMetrics(m);
            log("controller on display " + display.getDisplayId() + " \"" + display.getName() + "\" " + m.widthPixels + "x"
                + m.heightPixels + " rotation " + display.getRotation() + " " + m.densityDpi + " dpi");
        } catch (Throwable t) {
            log("controller could not be shown on display " + display.getDisplayId() + ": " + t);
            pads = null;
        }
    }

    private void dismiss() {
        if (pads != null) {
            if (pads.view != null) {
                pads.view.releaseAll();
            }
            try {
                pads.dismiss();
            } catch (Throwable ignored) {
                // gone already
            }
            pads = null;
            visible(false);
            log("controller removed");
        }
    }

    private void visible(boolean on) {
        if (ready) {
            Bo1Activity.ssVisible(on);
        }
    }

    // ---- what the controller's buttons do -------------------------------------------------------------------------
    void hidePads() {
        hidden = true;
        log("pads hidden");
        if (pads != null) {
            pads.refresh();
        }
    }

    void showRemap() {
        remapping = true;
        if (pads != null) {
            pads.refresh();
        }
        requestBinds();
    }

    void hideRemap() {
        remapping = false;
        if (pads != null) {
            pads.refresh();
        }
    }

    void settingsChanged() {
        settings.save();
        if (pads != null) {
            pads.view.applySettings();
        }
    }

    /** The Presentation: the pads and the remapping screen full size, or (hidden) only a small PADS button. */
    private final class Pads extends Presentation {
        SecondScreenView view;
        SecondScreenRemap remap;
        private FrameLayout full;
        private TextView small;

        Pads(Context outer, Display display) {
            super(outer, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            // never the focused window: the game keeps the controller's and the keyboard's events
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            view = new SecondScreenView(getContext(), SecondScreen.this);
            full = new FrameLayout(getContext());
            full.addView(view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            small = new TextView(getContext());
            small.setText("PADS");
            small.setTextColor(0xffc9d1db);
            small.setTextSize(16);
            small.setGravity(Gravity.CENTER);
            final int p = Math.round(14 * getContext().getResources().getDisplayMetrics().density);
            small.setPadding(p * 2, p, p * 2, p);
            small.setBackgroundColor(0xcc171b21);
            small.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    hidden = false;
                    log("pads shown");
                    refresh();
                }
            });
            refresh();
        }

        /** Hidden or full, pads or remapping screen. */
        void refresh() {
            if (full == null) {
                return;
            }
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            if (hidden) {
                view.releaseAll();
                if (small.getParent() == null) {
                    setContentView(small);
                }
                lp.width = ViewGroup.LayoutParams.WRAP_CONTENT;
                lp.height = ViewGroup.LayoutParams.WRAP_CONTENT;
                lp.gravity = Gravity.TOP | Gravity.END;
                getWindow().setAttributes(lp);
                visible(false);
                return;
            }
            if (full.getParent() == null) {
                setContentView(full);
            }
            lp.width = ViewGroup.LayoutParams.MATCH_PARENT;
            lp.height = ViewGroup.LayoutParams.MATCH_PARENT;
            getWindow().setAttributes(lp);
            if (remapping) {
                view.releaseAll();
                if (remap == null) {
                    remap = new SecondScreenRemap(getContext(), SecondScreen.this);
                    full.addView(remap, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                }
                remap.setVisibility(View.VISIBLE);
                remap.show();
                view.setVisibility(View.GONE);
                visible(false);
            } else {
                if (remap != null) {
                    remap.setVisibility(View.GONE);
                }
                view.setVisibility(View.VISIBLE);
                view.wake();
                view.invalidate();
                visible(true);
            }
        }
    }
}
