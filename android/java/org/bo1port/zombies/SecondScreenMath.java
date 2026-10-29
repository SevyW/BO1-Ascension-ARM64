package org.bo1port.zombies;

/**
 * The second-screen controller's geometry and gesture maths, without Android classes, so that it runs on the build
 * host too (android/test, tools/native/test-second-screen.sh). Screen coordinates: x right, y down, pixels of the
 * second display.
 */
final class SecondScreenMath {
    private SecondScreenMath() {
    }

    static final class Box {
        final float left, top, right, bottom;

        Box(float left, float top, float right, float bottom) {
            this.left = left;
            this.top = top;
            this.right = right;
            this.bottom = bottom;
        }

        float centerX() {
            return (left + right) * 0.5f;
        }

        float centerY() {
            return (top + bottom) * 0.5f;
        }

        float width() {
            return right - left;
        }

        float height() {
            return bottom - top;
        }

        boolean contains(float x, float y) {
            return x >= left && x < right && y >= top && y < bottom;
        }

        @Override
        public String toString() {
            return Math.round(left) + "," + Math.round(top) + "-" + Math.round(right) + "," + Math.round(bottom);
        }
    }

    /**
     * Where everything is, for a display of any shape: a bar along the top (HIDE left, REMAP right) and one
     * trackpad filling the rest.
     */
    static final class Layout {
        final int width, height;
        final Box bar, hide, remap, pad;

        Layout(int width, int height) {
            this.width = width;
            this.height = height;
            final float m = Math.round(Math.min(width, height) * 0.02f);
            final float barH = Math.round(Math.max(56f, height * 0.08f));
            bar = new Box(0, 0, width, barH);
            final float buttonW = Math.round(width * 0.22f);
            hide = new Box(m, m * 0.5f, m + buttonW, barH - m * 0.5f);
            remap = new Box(width - m - buttonW, m * 0.5f, width - m, barH - m * 0.5f);
            pad = new Box(m, barH + m * 0.5f, width - m, height - m);
        }

        @Override
        public String toString() {
            return width + "x" + height + " pad " + pad + " HIDE " + hide + " REMAP " + remap;
        }
    }

    /** Look gain: sensitivity, rising with finger speed (pixels a second) by accel, at most fourfold the base. */
    static float lookGain(float speed, float sensitivity, float accel) {
        final float fast = Math.min(Math.max(speed, 0f) / 1000f, 3f);
        return sensitivity * (1f + accel * fast);
    }

    /**
     * A pad click: a double tap (the second touch within doubleTapMs of the first one's release, near it), or a
     * finger held still for holdMs. The click lasts until that finger lifts. (The size press is Press, below.)
     */
    static final class Click {
        static final int DOUBLE_TAP = 0, HOLD = 1, PRESS = 2, OFF = 3;   // OFF: the pad only looks
        int method = DOUBLE_TAP;
        long holdMs = 350, doubleTapMs = 300;
        float slop = 24f;   // pixels a finger may move and still count as held still / the same tap

        private long downAt, lastUpAt = -1000000;
        private float downX, downY, lastUpX, lastUpY;
        private boolean moved, clicked, tapStarted;

        /** A finger touches down; true if this is a click (the second tap of a double tap). */
        boolean down(long now, float x, float y) {
            downAt = now;
            downX = x;
            downY = y;
            moved = false;
            clicked = false;
            tapStarted = true;
            if (method == DOUBLE_TAP && now - lastUpAt <= doubleTapMs && near(x, y, lastUpX, lastUpY)) {
                clicked = true;
                lastUpAt = -1000000;
            }
            return clicked;
        }

        void move(float x, float y) {
            if (!near(x, y, downX, downY)) {
                moved = true;
            }
        }

        /** For HOLD: call when holdMs have passed since the touch; true if that makes a click. */
        boolean holdDue(long now) {
            if (method != HOLD || !tapStarted || clicked || moved || now - downAt < holdMs) {
                return false;
            }
            clicked = true;
            return true;
        }

        /** The finger lifts; true if a click ends (the caller releases what the click pressed). */
        boolean up(long now, float x, float y) {
            final boolean was = clicked;
            tapStarted = false;
            if (!clicked && !moved && now - downAt <= doubleTapMs) {
                lastUpAt = now;   // a short tap: the first half of a double tap
                lastUpX = x;
                lastUpY = y;
            }
            clicked = false;
            return was;
        }

        boolean clicked() {
            return clicked;
        }

        private boolean near(float x, float y, float x2, float y2) {
            return (x - x2) * (x - x2) + (y - y2) * (y - y2) <= slop * slop * 4f;
        }
    }

    /**
     * The size press: pressing the finger flat against the glass is a click. The touch panel has no pressure
     * sensor, but it reports the contact's size (ABS_MT_TOUCH_MAJOR, 0..255 on the AYN Thor's second screen): a
     * finger resting lightly measured 2 to 5, a thumb pressed flat 11 to 13, climbing there in about 0.2-0.3 s.
     * Sizes here are in those raw units.
     *
     * The finger's relaxed size is the smallest it has been since it last pressed. A press is a quick rise well above
     * it (by ratio and by minRise); a rise slower than riseMs is a finger rolling flatter, not a press, and becomes
     * the new relaxed size. A press lasts until the size falls halfway back, so the finger can press again without
     * lifting. Flattening a finger moves its center, so the look pauses while the size climbs and for freezeMs
     * around a press or a release (frozen()).
     */
    static final class Press {
        float ratio = 1.8f;      // a press is at least this many times the relaxed size ...
        float minRise = 5f;      // ... and at least this much above it
        long riseMs = 400;
        long freezeMs = 120;
        private float relaxed;
        private long lowAt, changedAt = -1000000;
        private boolean pressed;

        void down(long now, float size) {
            relaxed = size;
            lowAt = now;
            pressed = false;
            changedAt = -1000000;
        }

        /** A new size of the finger: 1 = it pressed, -1 = it let go, 0 = no change. */
        int size(long now, float size) {
            if (!pressed) {
                if (size <= relaxed + 1f) {
                    relaxed = Math.min(relaxed, size);
                    lowAt = now;
                    return 0;
                }
                if (size >= threshold() && now - lowAt <= riseMs) {
                    pressed = true;
                    changedAt = now;
                    return 1;
                }
                if (now - lowAt > riseMs) {
                    relaxed = size;   // a slow rise: the finger just rests flatter now
                    lowAt = now;
                }
                return 0;
            }
            if (size <= relaxed + (threshold() - relaxed) * 0.5f) {
                pressed = false;
                changedAt = now;
                lowAt = now;
                return -1;
            }
            return 0;
        }

        /** Lets go (the finger lifted); true if a press ends. */
        boolean up(long now) {
            final boolean was = pressed;
            pressed = false;
            changedAt = now;
            return was;
        }

        /** Whether the look should pause now: just after a press or release, or while the size climbs toward one. */
        boolean frozen(long now, float size) {
            if (now - changedAt < freezeMs) {
                return true;
            }
            return !pressed && now - lowAt <= riseMs && size >= relaxed + (threshold() - relaxed) * 0.7f;
        }

        boolean pressed() {
            return pressed;
        }

        float relaxed() {
            return relaxed;
        }

        float threshold() {
            return Math.max(relaxed * ratio, relaxed + minRise);
        }
    }
}
