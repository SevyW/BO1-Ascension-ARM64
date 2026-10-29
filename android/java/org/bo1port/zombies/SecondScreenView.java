package org.bo1port.zombies;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

/**
 * The second screen's controller view: one trackpad for looking, under a bar with HIDE and REMAP
 * (SecondScreenMath.Layout). The pad's click fires by default: the finger pressed flat against the glass
 * (SecondScreenMath.Press), or a double tap / a hold (SecondScreenMath.Click). It draws only when something changes
 * (a press, a finger landing or lifting, dimming), so it costs nothing while idle.
 */
final class SecondScreenView extends View {
    private static final int NONE = 0, PAD = 1, HIDE = 2, REMAP = 3;
    private static final int BACKGROUND = 0xff0b0d10, OUTLINE = 0xff3a414b, OUTLINE_LIVE = 0xff6f7c8c,
        PRESSED = 0xff16263a, TEXT = 0xff9aa5b4, ACCENT = 0xff5aa0ff;

    private final SecondScreen controller;
    private SecondScreenMath.Layout layout;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF box = new RectF();
    private final float density;

    // the pad's finger (the first one on it; others are ignored) and the bar's
    private int padId = -1, barId = -1, barRegion = NONE;
    private float padX, padY, size;
    private long padAt;
    private boolean padClicked;
    private final SecondScreenMath.Click click = new SecondScreenMath.Click();
    private final SecondScreenMath.Press press = new SecondScreenMath.Press();

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

    /** The click method from the settings (after a change on the remapping screen). */
    void applySettings() {
        SecondScreenSettings s = controller.settings;
        click.method = s.clickMethod == SecondScreenMath.Click.HOLD ? SecondScreenMath.Click.HOLD : SecondScreenMath.Click.DOUBLE_TAP;
        click.holdMs = s.holdMs;
        click.slop = 12f * density;
        press.minRise = s.pressDepth;
        releaseAll();
        invalidate();
        wake();
    }

    private boolean pressMode() {
        return controller.settings.clickMethod == SecondScreenMath.Click.PRESS;
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
        if (layout.pad.contains(x, y)) {
            return PAD;
        }
        return NONE;
    }

    /**
     * The contact size of pointer i (history entry h, or the current sample when h < 0) in the panel's raw units
     * (ABS_MT_TOUCH_MAJOR, 0..255). AXIS_SIZE is the raw size over the raw maximum on any calibration; the touch
     * major in pixels is not (the Thor's GEOMETRIC calibration gives it the display's diagonal as its range).
     */
    private static float sizeOf(MotionEvent e, int i, int h) {
        return (h < 0 ? e.getSize(i) : e.getHistoricalSize(i, h)) * 255f;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        wake();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int i = e.getActionIndex();
                final int id = e.getPointerId(i);
                final float x = e.getX(i), y = e.getY(i);
                final int region = regionAt(x, y);
                if (region == PAD && padId < 0) {
                    padDown(id, x, y, sizeOf(e, i, -1), e.getEventTime());
                } else if ((region == HIDE || region == REMAP) && barId < 0) {
                    barId = id;
                    barRegion = region;
                }
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                final int i = padId < 0 ? -1 : e.findPointerIndex(padId);
                if (i >= 0) {
                    // the samples batched since the last frame, each with its own size and time
                    for (int h = 0; h < e.getHistorySize(); ++h) {
                        padMove(e.getHistoricalX(i, h), e.getHistoricalY(i, h), sizeOf(e, i, h), e.getHistoricalEventTime(h));
                    }
                    padMove(e.getX(i), e.getY(i), sizeOf(e, i, -1), e.getEventTime());
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                final int i = e.getActionIndex();
                final int id = e.getPointerId(i);
                if (id == padId) {
                    padUp(e.getX(i), e.getY(i), e.getEventTime());
                } else if (id == barId) {
                    if (barRegion == HIDE && layout.hide.contains(e.getX(i), e.getY(i))) {
                        controller.hidePads();
                    } else if (barRegion == REMAP && layout.remap.contains(e.getX(i), e.getY(i))) {
                        controller.showRemap();
                    }
                    barId = -1;
                    barRegion = NONE;
                }
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

    private void padDown(int id, float x, float y, float s, long now) {
        padId = id;
        padX = x;
        padY = y;
        padAt = now;
        size = s;
        if (pressMode()) {
            press.down(now, s);
            SecondScreen.log("pad down: size " + s);
        } else if (click.down(now, x, y)) {
            clickDown();
        } else if (click.method == SecondScreenMath.Click.HOLD) {
            postDelayed(hold, click.holdMs);
        }
        invalidate();
    }

    private void padMove(float x, float y, float s, long now) {
        size = s;
        if (pressMode()) {
            final int change = press.size(now, s);
            if (change > 0) {
                clickDown();
                SecondScreen.log("press: size " + s + " relaxed " + press.relaxed() + " threshold " + press.threshold());
            } else if (change < 0) {
                clickUp();
                SecondScreen.log("press released: size " + s);
            }
        }
        final float dx = x - padX, dy = y - padY;
        if (dx == 0 && dy == 0) {
            return;
        }
        final long dt = Math.max(1, now - padAt);
        // while the finger flattens or relaxes its center moves: that is not aiming, so the view holds still
        if (!pressMode() || !press.frozen(now, s)) {
            final float speed = (float) Math.sqrt(dx * dx + dy * dy) * 1000f / dt;
            final float gain = SecondScreenMath.lookGain(speed, controller.settings.sensitivity, controller.settings.acceleration);
            controller.look(dx * gain, dy * gain);
        }
        if (!pressMode()) {
            click.move(x, y);
        }
        padX = x;
        padY = y;
        padAt = now;
    }

    private void padUp(float x, float y, long now) {
        removeCallbacks(hold);
        if (pressMode()) {
            if (press.up(now)) {
                clickUp();
            }
        } else if (click.up(now, x, y)) {
            clickUp();
        }
        padId = -1;
        invalidate();
    }

    private void clickDown() {
        if (!padClicked) {
            padClicked = true;
            controller.key(SecondScreenBindings.PAD_PRESS.code, true);
            controller.haptic(true);
            invalidate();
        }
    }

    private void clickUp() {
        if (padClicked) {
            padClicked = false;
            controller.key(SecondScreenBindings.PAD_PRESS.code, false);
            invalidate();
        }
    }

    /** Lets go of everything held (a cancelled gesture, a settings change, the view going away). */
    void releaseAll() {
        removeCallbacks(hold);
        clickUp();
        final long now = SystemClock.uptimeMillis();
        click.up(now, -10000, -10000);
        press.up(now);
        padId = barId = -1;
        barRegion = NONE;
    }

    private final Runnable hold = new Runnable() {
        @Override
        public void run() {
            if (padId >= 0 && click.holdDue(SystemClock.uptimeMillis())) {
                clickDown();
            }
        }
    };

    // ---------------------------------------------------------------------------------------------------------
    @Override
    protected void onDraw(Canvas canvas) {
        if (layout == null) {
            return;
        }
        // the bar
        text.setTextSize(14f * density);
        drawButton(canvas, layout.hide, "HIDE");
        drawButton(canvas, layout.remap, "REMAP");
        text.setColor(TEXT);
        canvas.drawText(controller.stateText(), layout.bar.centerX(), textY(layout.bar), text);
        // the pad: lit while it clicks
        final SecondScreenMath.Box b = layout.pad;
        final float r = 24f * density;
        box.set(b.left, b.top, b.right, b.bottom);
        if (padClicked) {
            fill.setColor(PRESSED);
            canvas.drawRoundRect(box, r, r, fill);
        }
        line.setColor(padClicked ? ACCENT : padId >= 0 ? OUTLINE_LIVE : OUTLINE);
        canvas.drawRoundRect(box, r, r, line);
        text.setColor(TEXT);
        text.setTextSize(12f * density);
        canvas.drawText(pressMode() ? "LOOK  ·  PRESS TO FIRE" : "LOOK", b.centerX(), b.bottom - 14f * density, text);
        text.setTextSize(14f * density);
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
}
