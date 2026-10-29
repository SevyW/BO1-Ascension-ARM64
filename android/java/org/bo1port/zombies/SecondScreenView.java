package org.bo1port.zombies;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.SparseIntArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * The second screen's controller view: two trackpads, four grip buttons and a bar with HIDE and REMAP
 * (SecondScreenMath.Layout). Fingers are followed by pointer id, so both pads and the grips work at once. It draws
 * only when something changes (a finger moves, a button is pressed, it dims), so it costs nothing while idle.
 */
final class SecondScreenView extends View {
    private static final int NONE = 0, LEFT = 1, RIGHT = 2, L4 = 3, L5 = 4, R4 = 5, R5 = 6, HIDE = 7, REMAP = 8;
    private static final int BACKGROUND = 0xff0b0d10, OUTLINE = 0xff3a414b, OUTLINE_LIVE = 0xff6f7c8c,
        GRIP = 0xff171b21, GRIP_DOWN = 0xff2f4a6b, TEXT = 0xff9aa5b4, ACCENT = 0xff5aa0ff;

    private final SecondScreen controller;
    private SecondScreenMath.Layout layout;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF box = new RectF();
    private final SparseIntArray regionOf = new SparseIntArray();
    private final boolean[] gripDown = new boolean[4];
    private final float density;

    // the right pad: look
    private int rightId = -1;
    private float rightX, rightY;
    private long rightAt;
    private final SecondScreenMath.Click rightClick = new SecondScreenMath.Click();
    private final SecondScreenMath.Inertia inertia = new SecondScreenMath.Inertia();
    private long inertiaAt;

    // the left pad: its mode
    private int leftId = -1;
    private float leftX, leftY, leftDownX, leftDownY;
    private final SecondScreenMath.Click leftClick = new SecondScreenMath.Click();
    private final SecondScreenMath.ScrollWheel wheel = new SecondScreenMath.ScrollWheel();
    private int zone = SecondScreenMath.DPAD_NONE, slice = -1, leftPressed;

    private boolean dimmed;

    SecondScreenView(Context context, SecondScreen controller) {
        super(context);
        this.controller = controller;
        density = context.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2f * density);
        text.setColor(TEXT);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTextSize(14f * density);
        setBackgroundColor(BACKGROUND);
        applySettings();
    }

    /** The click method and inertia from the settings (after a change on the remapping screen). */
    void applySettings() {
        SecondScreenSettings s = controller.settings;
        rightClick.method = leftClick.method = s.clickMethod;
        rightClick.holdMs = leftClick.holdMs = s.holdMs;
        rightClick.slop = leftClick.slop = 12f * density;
        inertia.friction = s.friction;
        releaseAll();
        invalidate();
        wake();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        layout = new SecondScreenMath.Layout(w, h);
        SecondScreen.log("layout " + layout);
    }

    // ---------------------------------------------------------------------------------------------------------
    // dimming: after the set time without a finger the whole view fades; the next touch brings it back
    private final Runnable dim = new Runnable() {
        @Override
        public void run() {
            dimmed = true;
            setAlpha(0.3f);
        }
    };

    void wake() {
        removeCallbacks(dim);
        if (dimmed) {
            dimmed = false;
            setAlpha(1f);
        }
        if (controller.settings.dimSeconds > 0) {
            postDelayed(dim, controller.settings.dimSeconds * 1000L);
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    private int regionAt(float x, float y) {
        if (layout == null) {
            return NONE;
        }
        if (layout.hide.contains(x, y)) {
            return HIDE;
        }
        if (layout.remap.contains(x, y)) {
            return REMAP;
        }
        if (layout.leftPad.contains(x, y)) {
            return LEFT;
        }
        if (layout.rightPad.contains(x, y)) {
            return RIGHT;
        }
        if (layout.l4.contains(x, y)) {
            return L4;
        }
        if (layout.l5.contains(x, y)) {
            return L5;
        }
        if (layout.r4.contains(x, y)) {
            return R4;
        }
        if (layout.r5.contains(x, y)) {
            return R5;
        }
        return NONE;
    }

    private static SecondScreenBindings.Input gripInput(int region) {
        return region == L4 ? SecondScreenBindings.L4 : region == L5 ? SecondScreenBindings.L5
            : region == R4 ? SecondScreenBindings.R4 : SecondScreenBindings.R5;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        final long now = SystemClock.uptimeMillis();
        wake();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int i = e.getActionIndex();
                final int id = e.getPointerId(i);
                final float x = e.getX(i), y = e.getY(i);
                final int region = regionAt(x, y);
                regionOf.put(id, region);
                down(region, id, x, y, now);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); ++i) {
                    final int id = e.getPointerId(i);
                    move(regionOf.get(id, NONE), id, e.getX(i), e.getY(i), now);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                final int i = e.getActionIndex();
                final int id = e.getPointerId(i);
                up(regionOf.get(id, NONE), id, e.getX(i), e.getY(i), now);
                regionOf.delete(id);
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
            default:
                break;
        }
        return true;
    }

    private void down(int region, int id, float x, float y, long now) {
        switch (region) {
            case RIGHT:
                if (rightId >= 0) {
                    return;
                }
                rightId = id;
                rightX = x;
                rightY = y;
                rightAt = now;
                inertia.stop();
                if (rightClick.down(now, x, y)) {
                    controller.key(SecondScreenBindings.RIGHT_CLICK.code, true);
                    controller.haptic(true);
                } else if (rightClick.method == SecondScreenMath.Click.HOLD) {
                    postDelayed(rightHold, rightClick.holdMs);
                }
                invalidate();
                break;
            case LEFT:
                if (leftId >= 0) {
                    return;
                }
                leftId = id;
                leftX = leftDownX = x;
                leftY = leftDownY = y;
                wheel.reset();
                slice = -1;
                zone = currentZone();
                if (!"radial".equals(controller.settings.leftMode) && !"off".equals(controller.settings.leftMode)) {
                    if (leftClick.down(now, x, y)) {
                        pressLeft();
                    } else if (leftClick.method == SecondScreenMath.Click.HOLD) {
                        postDelayed(leftHold, leftClick.holdMs);
                    }
                }
                move(LEFT, id, x, y, now);
                invalidate();
                break;
            case L4:
            case L5:
            case R4:
            case R5:
                gripDown[region - L4] = true;
                controller.key(gripInput(region).code, true);
                controller.haptic(true);
                invalidate();
                break;
            default:
                break;
        }
    }

    private void move(int region, int id, float x, float y, long now) {
        if (region == RIGHT && id == rightId) {
            final float dx = x - rightX, dy = y - rightY;
            if (dx == 0 && dy == 0) {
                return;
            }
            final long dt = Math.max(1, now - rightAt);
            final float speed = (float) Math.sqrt(dx * dx + dy * dy) * 1000f / dt;
            final float gain = SecondScreenMath.lookGain(speed, controller.settings.sensitivity, controller.settings.acceleration);
            controller.look(dx * gain, dy * gain);
            inertia.track(dx * gain, dy * gain, dt);
            rightClick.move(x, y);
            rightX = x;
            rightY = y;
            rightAt = now;
            invalidate();
        } else if (region == LEFT && id == leftId) {
            leftX = x;
            leftY = y;
            leftClick.move(x, y);
            final SecondScreenMath.Box pad = layout.leftPad;
            final float cx = x - pad.centerX(), cy = y - pad.centerY();
            final String mode = controller.settings.leftMode;
            if ("joystick".equals(mode)) {
                final float[] stick = SecondScreenMath.joystick(x - leftDownX, y - leftDownY,
                    pad.width() * controller.settings.stickRadius, controller.settings.stickDeadzone);
                controller.stick(stick[0], stick[1]);
            } else if ("dpad".equals(mode)) {
                final int z = currentZone();
                if (z != zone) {
                    zone = z;
                    if (z != SecondScreenMath.DPAD_NONE) {
                        controller.haptic(false);
                    }
                }
            } else if ("radial".equals(mode)) {
                final int s = SecondScreenMath.radialSlice(cx, cy, controller.settings.radialSlices, pad.width() * 0.15f);
                if (s != slice) {
                    slice = s;
                    if (s >= 0) {
                        controller.haptic(false);
                    }
                }
            } else if ("scroll".equals(mode)) {
                final int steps = wheel.move(cx, cy, pad.width() * 0.12f, 30f);
                for (int k = 0; k < Math.abs(steps); ++k) {
                    controller.tapKey(steps > 0 ? SecondScreenBindings.SCROLL_NEXT.code : SecondScreenBindings.SCROLL_PREVIOUS.code);
                    controller.haptic(false);
                }
            }
            invalidate();
        }
    }

    private void up(int region, int id, float x, float y, long now) {
        if (region == RIGHT && id == rightId) {
            removeCallbacks(rightHold);
            if (rightClick.up(now, x, y)) {
                controller.key(SecondScreenBindings.RIGHT_CLICK.code, false);
            } else if (controller.settings.inertia && inertia.release(now - rightAt)) {
                inertiaAt = now;
                postOnAnimation(coast);
            }
            rightId = -1;
            invalidate();
        } else if (region == LEFT && id == leftId) {
            removeCallbacks(leftHold);
            if (leftClick.up(now, x, y) && leftPressed != 0) {
                controller.key(leftPressed, false);
                leftPressed = 0;
            }
            final String mode = controller.settings.leftMode;
            if ("joystick".equals(mode)) {
                controller.stick(0, 0);
            } else if ("radial".equals(mode) && slice >= 0 && slice < SecondScreenBindings.RADIAL.length) {
                controller.tapKey(SecondScreenBindings.RADIAL[slice].code);
                controller.haptic(true);
            }
            leftId = -1;
            slice = -1;
            zone = SecondScreenMath.DPAD_NONE;
            invalidate();
        } else if (region >= L4 && region <= R5) {
            gripDown[region - L4] = false;
            controller.key(gripInput(region).code, false);
            invalidate();
        } else if (region == HIDE && layout.hide.contains(x, y)) {
            controller.hidePads();
        } else if (region == REMAP && layout.remap.contains(x, y)) {
            controller.showRemap();
        }
    }

    /** Lets go of everything held (a cancelled gesture, a settings change, the view going away). */
    void releaseAll() {
        removeCallbacks(rightHold);
        removeCallbacks(leftHold);
        if (rightClick.clicked()) {
            controller.key(SecondScreenBindings.RIGHT_CLICK.code, false);
        }
        if (leftPressed != 0) {
            controller.key(leftPressed, false);
            leftPressed = 0;
        }
        for (int g = 0; g < 4; ++g) {
            if (gripDown[g]) {
                gripDown[g] = false;
                controller.key(gripInput(L4 + g).code, false);
            }
        }
        rightClick.up(SystemClock.uptimeMillis(), -10000, -10000);
        leftClick.up(SystemClock.uptimeMillis(), -10000, -10000);
        controller.stick(0, 0);
        inertia.stop();
        regionOf.clear();
        rightId = leftId = -1;
        slice = -1;
        zone = SecondScreenMath.DPAD_NONE;
    }

    private int currentZone() {
        if (layout == null || leftId < 0) {
            return SecondScreenMath.DPAD_NONE;
        }
        final SecondScreenMath.Box pad = layout.leftPad;
        return SecondScreenMath.dpadZone(leftX - pad.centerX(), leftY - pad.centerY(), pad.width() * 0.12f);
    }

    /** A left pad click: the d-pad zone under the finger in d-pad mode, else the pad's own click. */
    private void pressLeft() {
        int code = SecondScreenBindings.LEFT_CLICK.code;
        if ("dpad".equals(controller.settings.leftMode)) {
            final int z = currentZone();
            if (z == SecondScreenMath.DPAD_NONE) {
                return;
            }
            code = SecondScreenBindings.LEFT_DPAD[z].code;
        }
        leftPressed = code;
        controller.key(code, true);
        controller.haptic(true);
    }

    private final Runnable rightHold = new Runnable() {
        @Override
        public void run() {
            if (rightId >= 0 && rightClick.holdDue(SystemClock.uptimeMillis())) {
                controller.key(SecondScreenBindings.RIGHT_CLICK.code, true);
                controller.haptic(true);
            }
        }
    };

    private final Runnable leftHold = new Runnable() {
        @Override
        public void run() {
            if (leftId >= 0) {
                // the touch-down point counts: a d-pad zone is chosen by where the finger rests
                if (leftClick.holdDue(SystemClock.uptimeMillis())) {
                    pressLeft();
                }
            }
        }
    };

    // trackball: one step per display frame while a flick lasts
    private final Runnable coast = new Runnable() {
        @Override
        public void run() {
            if (!inertia.active() || rightId >= 0) {
                return;
            }
            final long now = SystemClock.uptimeMillis();
            final float[] d = inertia.step(now - inertiaAt);
            inertiaAt = now;
            controller.look(d[0], d[1]);
            if (inertia.active()) {
                postOnAnimation(this);
            }
        }
    };

    // ---------------------------------------------------------------------------------------------------------
    @Override
    protected void onDraw(Canvas canvas) {
        if (layout == null) {
            return;
        }
        final float r = 14f * density;
        // the bar
        text.setTextSize(14f * density);
        drawButton(canvas, layout.hide, "HIDE");
        drawButton(canvas, layout.remap, "REMAP");
        text.setColor(TEXT);
        canvas.drawText(controller.stateText(), layout.bar.centerX(), textY(layout.bar), text);
        // grips
        final SecondScreenMath.Box[] grips = {layout.l4, layout.l5, layout.r4, layout.r5};
        final String[] names = {"L4", "L5", "R4", "R5"};
        for (int g = 0; g < 4; ++g) {
            fill.setColor(gripDown[g] ? GRIP_DOWN : GRIP);
            box.set(grips[g].left, grips[g].top, grips[g].right, grips[g].bottom);
            canvas.drawRoundRect(box, r, r, fill);
            text.setColor(gripDown[g] ? 0xffffffff : TEXT);
            canvas.drawText(names[g], grips[g].centerX(), textY(grips[g]), text);
        }
        // pads
        drawPad(canvas, layout.leftPad, leftId >= 0, modeName(controller.settings.leftMode));
        drawPad(canvas, layout.rightPad, rightId >= 0, "LOOK");
        drawLeftMode(canvas);
        fill.setColor(ACCENT);
        if (rightId >= 0) {
            canvas.drawCircle(rightX, rightY, 10f * density, fill);
        }
        if (leftId >= 0) {
            canvas.drawCircle(leftX, leftY, 10f * density, fill);
        }
    }

    private float textY(SecondScreenMath.Box b) {
        return b.centerY() - (text.descent() + text.ascent()) * 0.5f;
    }

    private void drawButton(Canvas canvas, SecondScreenMath.Box b, String label) {
        line.setColor(OUTLINE);
        box.set(b.left, b.top, b.right, b.bottom);
        canvas.drawRoundRect(box, 10f * density, 10f * density, line);
        text.setColor(TEXT);
        canvas.drawText(label, b.centerX(), textY(b), text);
    }

    private void drawPad(Canvas canvas, SecondScreenMath.Box b, boolean live, String label) {
        line.setColor(live ? OUTLINE_LIVE : OUTLINE);
        box.set(b.left, b.top, b.right, b.bottom);
        canvas.drawRoundRect(box, 24f * density, 24f * density, line);
        text.setColor(TEXT);
        text.setTextSize(12f * density);
        canvas.drawText(label, b.centerX(), b.bottom - 12f * density, text);
        text.setTextSize(14f * density);
    }

    private static String modeName(String mode) {
        return "joystick".equals(mode) ? "JOYSTICK" : "radial".equals(mode) ? "RADIAL" : "scroll".equals(mode) ? "SCROLL"
            : "off".equals(mode) ? "OFF" : "D-PAD";
    }

    // what the left pad's mode looks like: zone dividers, slices, the stick's ring
    private void drawLeftMode(Canvas canvas) {
        final SecondScreenMath.Box b = layout.leftPad;
        final String mode = controller.settings.leftMode;
        final float cx = b.centerX(), cy = b.centerY(), half = b.width() * 0.5f;
        line.setColor(OUTLINE);
        if ("dpad".equals(mode)) {
            canvas.drawLine(b.left, b.top, b.right, b.bottom, line);
            canvas.drawLine(b.right, b.top, b.left, b.bottom, line);
            if (zone != SecondScreenMath.DPAD_NONE) {
                final float[][] at = {{0, -0.6f}, {0, 0.6f}, {-0.6f, 0}, {0.6f, 0}};
                fill.setColor(GRIP_DOWN);
                canvas.drawCircle(cx + at[zone][0] * half, cy + at[zone][1] * half, half * 0.22f, fill);
            }
        } else if ("radial".equals(mode)) {
            final int n = controller.settings.radialSlices;
            for (int k = 0; k < n; ++k) {
                final double a = Math.toRadians(360.0 / n * (k + 0.5));
                canvas.drawLine(cx + (float) Math.sin(a) * half * 0.15f, cy - (float) Math.cos(a) * half * 0.15f,
                    cx + (float) Math.sin(a) * half * 0.95f, cy - (float) Math.cos(a) * half * 0.95f, line);
                final double m = Math.toRadians(360.0 / n * k);
                text.setColor(k == slice ? 0xffffffff : TEXT);
                if (k == slice) {
                    fill.setColor(GRIP_DOWN);
                    canvas.drawCircle(cx + (float) Math.sin(m) * half * 0.62f, cy - (float) Math.cos(m) * half * 0.62f, half * 0.16f, fill);
                }
                canvas.drawText(String.valueOf(k + 1), cx + (float) Math.sin(m) * half * 0.62f,
                    cy - (float) Math.cos(m) * half * 0.62f - (text.descent() + text.ascent()) * 0.5f, text);
            }
        } else if ("scroll".equals(mode)) {
            canvas.drawCircle(cx, cy, half * 0.6f, line);
        } else if ("joystick".equals(mode) && leftId >= 0) {
            canvas.drawCircle(leftDownX, leftDownY, b.width() * controller.settings.stickRadius, line);
        }
    }
}
