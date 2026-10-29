package org.bo1port.zombies;

import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The remapping screen on the second display: BUTTONS (every input and its action; a tap opens the list of
 * actions, a choice is bound at once), PADS (look sensitivity, acceleration, trackball, the left pad's mode, the
 * click method, haptics, dimming) and PRESETS (console default, three saved slots per mode). Plain views and no
 * text entry: the window cannot take focus (the game keeps the controller), so everything is a tap.
 */
final class SecondScreenRemap extends LinearLayout {
    private static final int BACKGROUND = 0xff0b0d10, PANEL = 0xff171b21, SELECTED = 0xff2f4a6b, TEXT = 0xffc9d1db,
        DIM = 0xff7d8794;

    private final SecondScreen controller;
    private final FrameLayout content;
    private final float density;
    private final Map<String, String> binds = new LinkedHashMap<String, String>();
    private String tab = "buttons";
    private SecondScreenBindings.Input picking;   // the input whose action is being chosen (null: none)
    private ScrollView shown;                     // the list on screen, and which one it is
    private String shownKey;
    private final Map<String, Integer> scrollOf = new LinkedHashMap<String, Integer>();   // where each list was left

    SecondScreenRemap(Context context, SecondScreen controller) {
        super(context);
        this.controller = controller;
        density = context.getResources().getDisplayMetrics().density;
        setOrientation(VERTICAL);
        setBackgroundColor(BACKGROUND);
        LinearLayout tabs = new LinearLayout(context);
        tabs.setOrientation(HORIZONTAL);
        tabs.addView(tabButton("BUTTONS", "buttons"), weight());
        tabs.addView(tabButton("PADS", "pads"), weight());
        tabs.addView(tabButton("PRESETS", "presets"), weight());
        tabs.addView(button("DONE", new OnClickListener() {
            @Override
            public void onClick(View v) {
                SecondScreenRemap.this.controller.hideRemap();
            }
        }), weight());
        addView(tabs, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        content = new FrameLayout(context);
        addView(content, new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));
        show();
    }

    /** The engine's binds arrived (SecondScreen.onBinds). */
    void setBinds(Map<String, String> current) {
        binds.putAll(current);
        if ("buttons".equals(tab) && picking == null) {
            show();
        }
    }

    void show() {
        if (shown != null) {
            scrollOf.put(shownKey, shown.getScrollY());
        }
        shownKey = picking != null ? "pick" : tab;
        content.removeAllViews();
        final ScrollView scroll = new ScrollView(getContext());
        shown = scroll;
        LinearLayout list = new LinearLayout(getContext());
        list.setOrientation(VERTICAL);
        int pad = dp(8);
        list.setPadding(pad, pad, pad, pad);
        scroll.addView(list);
        content.addView(scroll);
        if (picking != null) {
            showActions(list);
        } else if ("pads".equals(tab)) {
            showPads(list);
        } else if ("presets".equals(tab)) {
            showPresets(list);
        } else {
            showButtons(list);
        }
        final Integer y = picking != null ? null : scrollOf.get(shownKey);
        if (y != null) {
            scroll.post(new Runnable() {
                @Override
                public void run() {
                    scroll.scrollTo(0, y);
                }
            });
        }
    }

    // ---- BUTTONS ------------------------------------------------------------------------------------------------
    private void showButtons(LinearLayout list) {
        list.addView(note(controller.zombies() ? "Zombies binds (players/config.cfg)" : "Multiplayer binds (players/config_mp.cfg)"));
        for (final SecondScreenBindings.Input input : inputs()) {
            final String bound = binds.get(input.key);
            list.addView(row(input.label, bound == null ? "..." : SecondScreenBindings.label(bound), new OnClickListener() {
                @Override
                public void onClick(View v) {
                    picking = input;
                    show();
                }
            }));
        }
    }

    private void showActions(LinearLayout list) {
        final SecondScreenBindings.Input input = picking;
        list.addView(note(input.label + ": choose an action"));
        list.addView(row("Cancel", "", new OnClickListener() {
            @Override
            public void onClick(View v) {
                picking = null;
                show();
            }
        }));
        final String bound = binds.get(input.key);
        for (final SecondScreenBindings.Action action : SecondScreenBindings.ACTIONS) {
            View r = row(action.label, "", new OnClickListener() {
                @Override
                public void onClick(View v) {
                    binds.put(input.key, action.command);
                    controller.bind(input.key, action.command);
                    picking = null;
                    show();
                }
            });
            if (bound != null && bound.equalsIgnoreCase(action.command)) {
                r.setBackground(panel(SELECTED));
            }
            list.addView(r);
        }
    }

    /** The inputs the screen lists. */
    private List<SecondScreenBindings.Input> inputs() {
        return SecondScreenBindings.all();
    }

    // ---- PADS ---------------------------------------------------------------------------------------------------
    private void showPads(LinearLayout list) {
        final SecondScreenSettings s = controller.settings;
        list.addView(slider("Look sensitivity", 1, 50, Math.round(s.sensitivity * 10), 0.1f, "", new Changed() {
            @Override
            public void set(float v) {
                s.sensitivity = v;
            }
        }));
        list.addView(slider("Acceleration (fast swipes turn more)", 0, 20, Math.round(s.acceleration * 10), 0.1f, "", new Changed() {
            @Override
            public void set(float v) {
                s.acceleration = v;
            }
        }));
        list.addView(choice("Pad click", new String[] {"Press (flatten the finger)", "Double tap", "Press and hold"},
            s.clickMethod == SecondScreenMath.Click.PRESS ? 0 : s.clickMethod == SecondScreenMath.Click.DOUBLE_TAP ? 1 : 2, new Chosen() {
            @Override
            public void set(int i) {
                s.clickMethod = i == 0 ? SecondScreenMath.Click.PRESS : i == 1 ? SecondScreenMath.Click.DOUBLE_TAP : SecondScreenMath.Click.HOLD;
            }
        }));
        list.addView(slider("Press depth (lower = lighter press)", 3, 10, Math.round(s.pressDepth), 1f, "", new Changed() {
            @Override
            public void set(float v) {
                s.pressDepth = v;
            }
        }));
        list.addView(slider("Hold time", 15, 80, s.holdMs / 10, 10f, " ms", new Changed() {
            @Override
            public void set(float v) {
                s.holdMs = Math.round(v);
            }
        }));
        showFeels(list);
        list.addView(slider("Dim after (0 = never)", 0, 30, s.dimSeconds, 1f, " s", new Changed() {
            @Override
            public void set(float v) {
                s.dimSeconds = Math.round(v);
            }
        }));
    }

    /** Click feel: Off and the ten feels, one row each; a tap chooses it and plays it. */
    private void showFeels(LinearLayout list) {
        final SecondScreenSettings s = controller.settings;
        final LinearLayout box = new LinearLayout(getContext());
        box.setOrientation(VERTICAL);
        box.setBackground(panel(PANEL));
        box.setPadding(dp(12), dp(8), dp(12), dp(8));
        box.addView(text("Click feel (tap one to feel it)", TEXT, 15));
        final LinearLayout rows = new LinearLayout(getContext());
        rows.setOrientation(VERTICAL);
        final int count = SecondScreen.FEELS.length + 1;
        for (int i = 0; i < count; ++i) {
            final int index = i;   // 0 = off, then the feels
            TextView option = button(i == 0 ? "Off" : i + ". " + SecondScreen.FEELS[i - 1], new OnClickListener() {
                @Override
                public void onClick(View v) {
                    s.haptics = index > 0;
                    if (index > 0) {
                        s.clickFeel = index - 1;
                        controller.playFeel(s.clickFeel, true);
                        if (s.clickFeel == 6) {
                            postDelayed(new Runnable() {
                                @Override
                                public void run() {
                                    controller.playFeel(6, false);
                                }
                            }, 250);
                        }
                    }
                    controller.settingsChanged();
                    for (int k = 0; k < rows.getChildCount(); ++k) {
                        rows.getChildAt(k).setBackground(panel(k == index ? SELECTED : BACKGROUND));
                    }
                }
            });
            option.setBackground(panel(i == (s.haptics ? s.clickFeel + 1 : 0) ? SELECTED : BACKGROUND));
            LayoutParams lp = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
            lp.topMargin = dp(4);
            rows.addView(option, lp);
        }
        box.addView(rows);
        list.addView(spaced(box));
    }

    // ---- PRESETS ------------------------------------------------------------------------------------------------
    private void showPresets(LinearLayout list) {
        final String mode = controller.zombies() ? "zm" : "mp";
        list.addView(note((controller.zombies() ? "Zombies" : "Multiplayer") + " presets. Loading one binds every button at once."));
        list.addView(row("Reset to console default", "", new OnClickListener() {
            @Override
            public void onClick(View v) {
                controller.applyPreset(SecondScreenBindings.consoleDefault());
                binds.putAll(SecondScreenBindings.consoleDefault());
                show();
            }
        }));
        for (int slot = 1; slot <= SecondScreenSettings.PRESET_SLOTS; ++slot) {
            final String name = SecondScreenSettings.presetName(mode, slot);
            final Map<String, String> saved = controller.settings.presets.get(name);
            list.addView(row("Load Custom " + slot, saved == null ? "(empty)" : saved.size() + " binds", new OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (saved != null) {
                        controller.applyPreset(saved);
                        binds.putAll(saved);
                        show();
                    }
                }
            }));
            list.addView(row("Save current as Custom " + slot, "", new OnClickListener() {
                @Override
                public void onClick(View v) {
                    Map<String, String> copy = new LinkedHashMap<String, String>();
                    for (SecondScreenBindings.Input i : SecondScreenBindings.all()) {
                        String b = binds.get(i.key);
                        copy.put(i.key, b == null ? "" : b);
                    }
                    controller.settings.presets.put(name, copy);
                    controller.settings.save();
                    show();
                }
            }));
        }
    }

    // ---- small views --------------------------------------------------------------------------------------------
    private interface Changed {
        void set(float value);
    }

    private interface Chosen {
        void set(int index);
    }

    private View slider(final String label, int min, int max, int at, final float scale, final String unit, final Changed changed) {
        LinearLayout box = new LinearLayout(getContext());
        box.setOrientation(VERTICAL);
        box.setBackground(panel(PANEL));
        box.setPadding(dp(12), dp(8), dp(12), dp(8));
        final TextView title = text(label + ": " + format(at * scale) + unit, TEXT, 15);
        box.addView(title);
        final SeekBar bar = new SeekBar(getContext());
        final int low = min;
        bar.setMax(max - min);
        bar.setProgress(Math.max(0, Math.min(max - min, at - min)));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                final float value = (progress + low) * scale;
                title.setText(label + ": " + format(value) + unit);
                if (fromUser) {
                    changed.set(value);
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                controller.settingsChanged();
            }
        });
        box.addView(bar, new LayoutParams(LayoutParams.MATCH_PARENT, dp(40)));
        return spaced(box);
    }

    private View choice(String label, final String[] options, final int selected, final Chosen chosen) {
        LinearLayout box = new LinearLayout(getContext());
        box.setOrientation(VERTICAL);
        box.setBackground(panel(PANEL));
        box.setPadding(dp(12), dp(8), dp(12), dp(8));
        box.addView(text(label, TEXT, 15));
        final LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(HORIZONTAL);
        for (int i = 0; i < options.length; ++i) {
            final int index = i;
            TextView option = button(options[i], new OnClickListener() {
                @Override
                public void onClick(View v) {
                    chosen.set(index);
                    controller.settingsChanged();
                    for (int k = 0; k < row.getChildCount(); ++k) {
                        row.getChildAt(k).setBackground(panel(k == index ? SELECTED : BACKGROUND));
                    }
                }
            });
            option.setBackground(panel(i == selected ? SELECTED : BACKGROUND));
            row.addView(option, weight());
        }
        box.addView(row);
        return spaced(box);
    }

    private View row(String label, String value, OnClickListener click) {
        LinearLayout r = new LinearLayout(getContext());
        r.setOrientation(HORIZONTAL);
        r.setBackground(panel(PANEL));
        r.setPadding(dp(12), dp(12), dp(12), dp(12));
        r.setMinimumHeight(dp(48));
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.addView(text(label, TEXT, 15), new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f));
        if (value.length() > 0) {
            TextView v = text(value, DIM, 14);
            v.setGravity(Gravity.END);
            r.addView(v, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f));
        }
        r.setOnClickListener(click);
        return spaced(r);
    }

    private View note(String s) {
        TextView t = text(s, DIM, 13);
        t.setPadding(dp(4), dp(4), dp(4), dp(8));
        return t;
    }

    private TextView tabButton(String label, final String which) {
        TextView b = button(label, new OnClickListener() {
            @Override
            public void onClick(View v) {
                tab = which;
                picking = null;
                if ("buttons".equals(which)) {
                    SecondScreenRemap.this.controller.requestBinds();
                }
                show();
            }
        });
        return b;
    }

    private TextView button(String label, OnClickListener click) {
        TextView b = text(label, TEXT, 14);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(6), dp(14), dp(6), dp(14));
        b.setBackground(panel(PANEL));
        b.setOnClickListener(click);
        return b;
    }

    private TextView text(String s, int color, int sp) {
        TextView t = new TextView(getContext());
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(sp);
        return t;
    }

    private View spaced(View v) {
        LayoutParams p = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        p.setMargins(0, dp(3), 0, dp(3));
        v.setLayoutParams(p);
        return v;
    }

    private LayoutParams weight() {
        LayoutParams p = new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f);
        p.setMargins(dp(2), dp(2), dp(2), dp(2));
        return p;
    }

    private GradientDrawable panel(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(8));
        return d;
    }

    private int dp(float v) {
        return Math.round(v * density);
    }

    private static String format(float v) {
        return v == Math.round(v) ? String.valueOf(Math.round(v)) : String.format(java.util.Locale.ROOT, "%.1f", v);
    }
}
