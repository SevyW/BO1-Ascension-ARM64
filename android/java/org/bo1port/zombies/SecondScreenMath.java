package org.bo1port.zombies;

/**
 * The second-screen controller's geometry and gesture maths, without Android classes, so that it runs on the build
 * host too (android/test, tools/native/test-second-screen.sh). Screen coordinates: x right, y down, pixels of the
 * second display. Stick values: -1..1 with y up, as the engine's pad state.
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
     * Where everything is, for a display of any shape: a bar along the top (HIDE left, REMAP right), the grip
     * buttons as two columns along the left and right edges (L4 over L5, R4 over R5), and two square trackpads
     * side by side between them, as large as fits.
     */
    static final class Layout {
        final int width, height;
        final Box bar, hide, remap, leftPad, rightPad, l4, l5, r4, r5;

        Layout(int width, int height) {
            this.width = width;
            this.height = height;
            final float m = Math.round(Math.min(width, height) * 0.02f);
            final float barH = Math.round(Math.max(56f, height * 0.08f));
            bar = new Box(0, 0, width, barH);
            final float buttonW = Math.round(width * 0.22f);
            hide = new Box(m, m * 0.5f, m + buttonW, barH - m * 0.5f);
            remap = new Box(width - m - buttonW, m * 0.5f, width - m, barH - m * 0.5f);
            final float top = barH + m, bottom = height - m;
            final float gripW = Math.round(width * (width > height ? 0.07f : 0.09f));
            final float middle = (top + bottom) * 0.5f;
            l4 = new Box(m, top, m + gripW, middle - m * 0.5f);
            l5 = new Box(m, middle + m * 0.5f, m + gripW, bottom);
            r4 = new Box(width - m - gripW, top, width - m, middle - m * 0.5f);
            r5 = new Box(width - m - gripW, middle + m * 0.5f, width - m, bottom);
            final float padsLeft = m + gripW + m, padsRight = width - m - gripW - m;
            final float side = Math.max(1f, Math.min((padsRight - padsLeft - m) * 0.5f, bottom - top));
            final float padTop = Math.round(middle - side * 0.5f);
            leftPad = new Box(padsLeft, padTop, padsLeft + side, padTop + side);
            rightPad = new Box(padsRight - side, padTop, padsRight, padTop + side);
        }

        @Override
        public String toString() {
            return width + "x" + height + " leftPad " + leftPad + " rightPad " + rightPad + " L4 " + l4 + " L5 " + l5
                + " R4 " + r4 + " R5 " + r5 + " HIDE " + hide + " REMAP " + remap;
        }
    }

    /**
     * The left pad as a joystick: the finger's offset from where it touched down, as a stick deflection. Within
     * deadzone (a fraction of radius) it is 0; from there to radius it rises to 1; beyond, 1 in that direction.
     */
    static float[] joystick(float dx, float dy, float radius, float deadzone) {
        final float length = (float) Math.sqrt(dx * dx + dy * dy);
        if (radius <= 0 || length <= deadzone * radius) {
            return new float[] {0, 0};
        }
        float amount = (length / radius - deadzone) / (1f - deadzone);
        if (amount > 1f) {
            amount = 1f;
        }
        return new float[] {dx / length * amount, -dy / length * amount};
    }

    /**
     * The radial slice under a finger at (dx, dy) from the pad's center: slice 0 is centered at the top, the others
     * follow clockwise. -1 within innerRadius (no choice yet).
     */
    static int radialSlice(float dx, float dy, int slices, float innerRadius) {
        if (slices <= 0 || dx * dx + dy * dy <= innerRadius * innerRadius) {
            return -1;
        }
        // degrees clockwise from straight up (screen y points down)
        double angle = Math.toDegrees(Math.atan2(dx, -dy));
        final double width = 360.0 / slices;
        angle = (angle + width * 0.5 + 360.0) % 360.0;
        int slice = (int) Math.floor(angle / width);
        return slice >= slices ? slices - 1 : slice;
    }

    static final int DPAD_NONE = -1, DPAD_UP = 0, DPAD_DOWN = 1, DPAD_LEFT = 2, DPAD_RIGHT = 3;

    /** The d-pad zone under a finger at (dx, dy) from the pad's center: the nearer axis wins; none within innerRadius. */
    static int dpadZone(float dx, float dy, float innerRadius) {
        if (dx * dx + dy * dy <= innerRadius * innerRadius) {
            return DPAD_NONE;
        }
        if (Math.abs(dx) > Math.abs(dy)) {
            return dx > 0 ? DPAD_RIGHT : DPAD_LEFT;
        }
        return dy > 0 ? DPAD_DOWN : DPAD_UP;
    }

    /** Look gain: sensitivity, rising with finger speed (pixels a second) by accel, at most fourfold the base. */
    static float lookGain(float speed, float sensitivity, float accel) {
        final float fast = Math.min(Math.max(speed, 0f) / 1000f, 3f);
        return sensitivity * (1f + accel * fast);
    }

    /** The scroll wheel: a finger circling the pad's center; one step per stepDegrees, clockwise positive. */
    static final class ScrollWheel {
        private double lastAngle;
        private double turned;
        private boolean tracking;

        void reset() {
            tracking = false;
            turned = 0;
        }

        /** Steps made by the move to (dx, dy) from the center (none near the center, where the angle is unsteady). */
        int move(float dx, float dy, float minRadius, float stepDegrees) {
            if (dx * dx + dy * dy < minRadius * minRadius) {
                tracking = false;
                return 0;
            }
            final double angle = Math.toDegrees(Math.atan2(dy, dx));   // y down: increases clockwise
            if (!tracking) {
                tracking = true;
                lastAngle = angle;
                return 0;
            }
            double delta = angle - lastAngle;
            if (delta > 180) {
                delta -= 360;
            } else if (delta < -180) {
                delta += 360;
            }
            lastAngle = angle;
            turned += delta;
            int steps = 0;
            final double almost = stepDegrees - 1e-6;   // whole turns made of many small moves still count
            while (turned >= almost) {
                turned -= stepDegrees;
                ++steps;
            }
            while (turned <= -almost) {
                turned += stepDegrees;
                --steps;
            }
            return steps;
        }
    }

    /**
     * A pad click: a double tap (the second touch within doubleTapMs of the first one's release, near it), or a
     * finger held still for holdMs. The click lasts until that finger lifts.
     */
    static final class Click {
        static final int DOUBLE_TAP = 0, HOLD = 1;
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
     * Trackball inertia of the look pad: the finger's velocity is followed while it moves; after a flick the view
     * keeps turning at that velocity, which falls by exp(-friction * seconds).
     */
    static final class Inertia {
        float friction = 4f;        // 1/s
        float minStartSpeed = 300f; // pixels a second: slower releases stop dead
        float stopSpeed = 20f;
        private float vx, vy;
        private boolean active;

        /** A move of (dx, dy) in dtMs while the finger is down. */
        void track(float dx, float dy, float dtMs) {
            if (dtMs <= 0) {
                return;
            }
            final float k = Math.min(1f, dtMs / 40f);   // about the last 40 ms
            vx += (dx * 1000f / dtMs - vx) * k;
            vy += (dy * 1000f / dtMs - vy) * k;
        }

        void stop() {
            active = false;
            vx = vy = 0;
        }

        /** The finger lifted after the last move ago ms: true if a flick keeps the view turning. */
        boolean release(float sinceLastMoveMs) {
            if (sinceLastMoveMs > 60f || Math.sqrt(vx * vx + vy * vy) < minStartSpeed) {
                stop();
                return false;
            }
            active = true;
            return true;
        }

        boolean active() {
            return active;
        }

        float speed() {
            return (float) Math.sqrt(vx * vx + vy * vy);
        }

        /** The movement of the next dtMs (pixels), with the decay applied; inactive once slow enough. */
        float[] step(float dtMs) {
            if (!active || dtMs <= 0) {
                return new float[] {0, 0};
            }
            final float seconds = dtMs / 1000f;
            final float[] d = {vx * seconds, vy * seconds};
            final float decay = (float) Math.exp(-friction * seconds);
            vx *= decay;
            vy *= decay;
            if (speed() < stopSpeed) {
                active = false;
            }
            return d;
        }
    }
}
