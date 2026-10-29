package org.bo1port.zombies;

import java.util.List;
import java.util.Map;

/**
 * Off-device tests of the second-screen controller's maths and tables (SecondScreenMath, SecondScreenBindings).
 * Plain Java, no test framework: tools/native/test-second-screen.sh compiles and runs it; exit code 1 on a failure.
 */
public final class SecondScreenMathTest {
    private static int failures, checks;

    private static void check(boolean ok, String what) {
        ++checks;
        if (!ok) {
            ++failures;
            System.out.println("FAIL: " + what);
        }
    }

    private static void near(float a, float b, float eps, String what) {
        check(Math.abs(a - b) <= eps, what + " (" + a + " vs " + b + ")");
    }

    public static void main(String[] args) {
        layout();
        joystick();
        radial();
        dpad();
        scroll();
        click();
        inertia();
        gain();
        bindings();
        System.out.println(checks + " checks, " + failures + " failures");
        System.exit(failures == 0 ? 0 : 1);
    }

    static void layout() {
        for (int[] size : new int[][] {{1080, 1240}, {1240, 1080}, {720, 1280}, {1920, 1080}}) {
            SecondScreenMath.Layout l = new SecondScreenMath.Layout(size[0], size[1]);
            final String name = size[0] + "x" + size[1];
            System.out.println("layout " + l);
            SecondScreenMath.Box[] all = {l.hide, l.remap, l.leftPad, l.rightPad, l.l4, l.l5, l.r4, l.r5};
            for (SecondScreenMath.Box b : all) {
                check(b.left >= 0 && b.top >= 0 && b.right <= size[0] && b.bottom <= size[1] && b.width() > 0 && b.height() > 0,
                    name + ": box inside the screen " + b);
            }
            for (int i = 0; i < all.length; ++i) {
                for (int j = i + 1; j < all.length; ++j) {
                    SecondScreenMath.Box a = all[i], b = all[j];
                    boolean overlap = a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
                    check(!overlap, name + ": boxes " + i + " and " + j + " do not overlap");
                }
            }
            near(l.leftPad.width(), l.leftPad.height(), 0.5f, name + ": left pad square");
            near(l.leftPad.width(), l.rightPad.width(), 0.5f, name + ": pads the same size");
            check(l.leftPad.right <= l.rightPad.left, name + ": left pad left of the right pad");
            check(l.l4.bottom <= l.l5.top && l.r4.bottom <= l.r5.top, name + ": L4 over L5, R4 over R5");
        }
        // the Thor's second display (1080x1240): pads of about 388 px
        SecondScreenMath.Layout thor = new SecondScreenMath.Layout(1080, 1240);
        check(thor.leftPad.width() > 350, "Thor: pads at least 350 px (" + thor.leftPad.width() + ")");
    }

    static void joystick() {
        float[] s = SecondScreenMath.joystick(0, 0, 100, 0.1f);
        near(s[0], 0, 0, "joystick: centre 0");
        s = SecondScreenMath.joystick(5, 0, 100, 0.1f);
        near(s[0], 0, 0, "joystick: inside the dead zone 0");
        s = SecondScreenMath.joystick(100, 0, 100, 0.1f);
        near(s[0], 1, 1e-5f, "joystick: at the radius full right");
        s = SecondScreenMath.joystick(0, -300, 100, 0.1f);
        near(s[1], 1, 1e-5f, "joystick: beyond the radius, finger up = stick up, clamped");
        near(s[0], 0, 1e-5f, "joystick: straight up has no x");
        s = SecondScreenMath.joystick(55, 0, 100, 0.1f);
        near(s[0], 0.5f, 1e-5f, "joystick: halfway between dead zone and radius = 0.5");
        s = SecondScreenMath.joystick(-70.71f, 70.71f, 100, 0f);
        near(s[0], -0.7071f, 1e-3f, "joystick: diagonal down-left x");
        near(s[1], -0.7071f, 1e-3f, "joystick: diagonal down-left y (down = negative)");
    }

    static void radial() {
        check(SecondScreenMath.radialSlice(0, -100, 8, 20) == 0, "radial: straight up is slice 0");
        check(SecondScreenMath.radialSlice(100, 0, 8, 20) == 2, "radial 8: right is slice 2");
        check(SecondScreenMath.radialSlice(0, 100, 8, 20) == 4, "radial 8: down is slice 4");
        check(SecondScreenMath.radialSlice(-100, 0, 8, 20) == 6, "radial 8: left is slice 6");
        check(SecondScreenMath.radialSlice(100, 0, 4, 20) == 1, "radial 4: right is slice 1");
        check(SecondScreenMath.radialSlice(-100, -1, 4, 20) == 3, "radial 4: left is slice 3");
        check(SecondScreenMath.radialSlice(5, 5, 8, 20) == -1, "radial: inside the inner circle none");
        // slice 0 spans -22.5 .. +22.5 degrees with 8 slices
        double a = Math.toRadians(20);
        check(SecondScreenMath.radialSlice((float) Math.sin(a) * 100, (float) -Math.cos(a) * 100, 8, 20) == 0, "radial: 20 deg is slice 0");
        a = Math.toRadians(-20);
        check(SecondScreenMath.radialSlice((float) Math.sin(a) * 100, (float) -Math.cos(a) * 100, 8, 20) == 0, "radial: -20 deg is slice 0");
        a = Math.toRadians(25);
        check(SecondScreenMath.radialSlice((float) Math.sin(a) * 100, (float) -Math.cos(a) * 100, 8, 20) == 1, "radial: 25 deg is slice 1");
        a = Math.toRadians(-25);
        check(SecondScreenMath.radialSlice((float) Math.sin(a) * 100, (float) -Math.cos(a) * 100, 8, 20) == 7, "radial: -25 deg is slice 7");
        for (int n : new int[] {4, 6, 8}) {
            for (int deg = 0; deg < 360; deg += 7) {
                double r = Math.toRadians(deg);
                int s = SecondScreenMath.radialSlice((float) Math.sin(r) * 50, (float) -Math.cos(r) * 50, n, 10);
                check(s >= 0 && s < n, "radial " + n + ": " + deg + " deg in range");
            }
        }
    }

    static void dpad() {
        check(SecondScreenMath.dpadZone(0, -50, 10) == SecondScreenMath.DPAD_UP, "dpad: up");
        check(SecondScreenMath.dpadZone(0, 50, 10) == SecondScreenMath.DPAD_DOWN, "dpad: down");
        check(SecondScreenMath.dpadZone(-50, 10, 10) == SecondScreenMath.DPAD_LEFT, "dpad: left");
        check(SecondScreenMath.dpadZone(50, -10, 10) == SecondScreenMath.DPAD_RIGHT, "dpad: right");
        check(SecondScreenMath.dpadZone(3, 3, 10) == SecondScreenMath.DPAD_NONE, "dpad: centre none");
        check(SecondScreenMath.dpadZone(40, -41, 10) == SecondScreenMath.DPAD_UP, "dpad: nearer axis wins");
    }

    static void scroll() {
        SecondScreenMath.ScrollWheel w = new SecondScreenMath.ScrollWheel();
        int total = 0;
        // a full clockwise circle on screen (y down: right, down, left, up), in 5 degree moves
        for (int deg = 0; deg <= 360; deg += 5) {
            double r = Math.toRadians(deg);
            total += w.move((float) Math.cos(r) * 100, (float) Math.sin(r) * 100, 20, 30);
        }
        check(total == 12, "scroll: one clockwise turn = 12 steps of 30 deg (" + total + ")");
        w.reset();
        total = 0;
        for (int deg = 0; deg >= -95; deg -= 5) {
            double r = Math.toRadians(deg);
            total += w.move((float) Math.cos(r) * 100, (float) Math.sin(r) * 100, 20, 30);
        }
        check(total == -3, "scroll: 95 deg counter-clockwise = -3 steps (" + total + ")");
        w.reset();
        total = 0;
        // crossing the +-180 degree line counts the short way
        for (int deg = 170; deg <= 200; deg += 5) {
            double r = Math.toRadians(deg);
            total += w.move((float) Math.cos(r) * 100, (float) Math.sin(r) * 100, 20, 30);
        }
        check(total == 1, "scroll: across 180 deg = 1 step (" + total + ")");
        check(w.move(1, 1, 20, 30) == 0, "scroll: near the centre nothing");
    }

    static void click() {
        SecondScreenMath.Click c = new SecondScreenMath.Click();
        c.method = SecondScreenMath.Click.DOUBLE_TAP;
        check(!c.down(1000, 50, 50), "double tap: the first touch is no click");
        check(!c.up(1080, 50, 50), "double tap: the first release ends nothing");
        check(c.down(1250, 52, 51), "double tap: the second touch within 300 ms is a click");
        check(c.clicked(), "double tap: clicked while held");
        check(c.up(1600, 52, 51), "double tap: its release ends the click");
        check(!c.down(1700, 50, 50), "double tap: a third touch starts over");
        c.up(1750, 50, 50);
        check(!c.down(2200, 50, 50), "double tap: too late (450 ms) is no click");
        c.up(2250, 50, 50);
        check(!c.down(2400, 300, 300), "double tap: too far away is no click");
        c.up(2450, 300, 300);
        // a swipe is not the first tap of a double tap
        c.down(3000, 50, 50);
        c.move(200, 50);
        c.up(3100, 200, 50);
        check(!c.down(3200, 200, 50), "double tap: after a swipe no click");
        c.up(3250, 200, 50);

        SecondScreenMath.Click h = new SecondScreenMath.Click();
        h.method = SecondScreenMath.Click.HOLD;
        h.holdMs = 350;
        check(!h.down(1000, 50, 50), "hold: touching is no click");
        check(!h.holdDue(1200), "hold: 200 ms is too early");
        check(h.holdDue(1360), "hold: 360 ms held still is a click");
        check(!h.holdDue(1400), "hold: only once");
        check(h.up(1500, 50, 50), "hold: release ends it");
        h.down(2000, 50, 50);
        h.move(120, 50);
        check(!h.holdDue(2400), "hold: a moving finger is no click");
        check(!h.up(2450, 120, 50), "hold: nothing to end");
    }

    static void inertia() {
        SecondScreenMath.Inertia in = new SecondScreenMath.Inertia();
        in.friction = 4f;
        // a flick: 20 px every 8 ms (2500 px/s) for 160 ms (the estimate follows about the last 40 ms)
        for (int i = 0; i < 20; ++i) {
            in.track(20, 0, 8);
        }
        near(in.speed(), 2500, 200, "inertia: follows the finger's speed");
        check(in.release(5), "inertia: a fast release starts coasting");
        float moved = 0;
        float firstStep = in.step(8)[0];
        moved += firstStep;
        near(firstStep, 20, 3, "inertia: first frame continues at the finger's speed");
        int frames = 1;
        while (in.active() && frames < 10000) {
            moved += in.step(8)[0];
            ++frames;
        }
        check(!in.active(), "inertia: stops by itself");
        // v0 / friction = 2500 / 4 = 625 px in total (the continuous decay), less the slow tail
        near(moved, 625, 40, "inertia: total coast distance about v / friction");
        check(frames > 50 && frames < 200, "inertia: lasts about a second (" + frames + " frames of 8 ms)");
        SecondScreenMath.Inertia slow = new SecondScreenMath.Inertia();
        slow.track(1, 0, 8);
        check(!slow.release(5), "inertia: a slow release does not coast");
        SecondScreenMath.Inertia late = new SecondScreenMath.Inertia();
        for (int i = 0; i < 10; ++i) {
            late.track(20, 0, 8);
        }
        check(!late.release(200), "inertia: a finger that stopped before lifting does not coast");
        SecondScreenMath.Inertia high = new SecondScreenMath.Inertia();
        high.friction = 10f;
        for (int i = 0; i < 10; ++i) {
            high.track(20, 0, 8);
        }
        high.release(5);
        float highMoved = 0;
        while (high.active()) {
            highMoved += high.step(8)[0];
        }
        check(highMoved < moved * 0.5f, "inertia: more friction, shorter coast");
    }

    static void gain() {
        near(SecondScreenMath.lookGain(0, 1.5f, 0.5f), 1.5f, 1e-6f, "gain: at rest the sensitivity");
        near(SecondScreenMath.lookGain(1000, 1f, 0.5f), 1.5f, 1e-6f, "gain: 1000 px/s with accel 0.5 = 1.5x");
        near(SecondScreenMath.lookGain(100000, 1f, 1f), 4f, 1e-6f, "gain: capped at 3x extra");
        near(SecondScreenMath.lookGain(5000, 2f, 0f), 2f, 1e-6f, "gain: no acceleration, flat");
    }

    static void bindings() {
        List<SecondScreenBindings.Input> all = SecondScreenBindings.all();
        check(all.size() == 16 + 4 + 2 + 4 + 2 + 8, "bindings: 36 inputs (" + all.size() + ")");
        java.util.HashSet<String> keys = new java.util.HashSet<String>();
        java.util.HashSet<Integer> codes = new java.util.HashSet<Integer>();
        for (SecondScreenBindings.Input i : all) {
            check(keys.add(i.key), "bindings: key " + i.key + " once");
            if (i.code != 0) {
                check(codes.add(i.code), "bindings: code " + i.code + " once");
            }
        }
        // key codes as src/client/cl_keys.cpp names them
        check(SecondScreenBindings.L4.code == 207 && "AUX1".equals(SecondScreenBindings.L4.key), "bindings: AUX1 = 207");
        check(SecondScreenBindings.SCROLL_PREVIOUS.code == 218 && "AUX12".equals(SecondScreenBindings.SCROLL_PREVIOUS.key), "bindings: AUX12 = 218");
        check(SecondScreenBindings.RADIAL[0].code == 182 && SecondScreenBindings.RADIAL[7].code == 189, "bindings: KP_HOME 182 .. KP_DOWNARROW 189");
        Map<String, String> d = SecondScreenBindings.consoleDefault();
        for (SecondScreenBindings.Input i : all) {
            check(d.containsKey(i.key), "bindings: the default preset binds " + i.key);
        }
        check("+gostand".equals(d.get("BUTTON_A")) && "+attack".equals(d.get("BUTTON_RTRIG")), "bindings: console default A / RT");
        check("bind AUX1 \"+gostand\"".equals(SecondScreenBindings.command("AUX1", "+gostand")), "bindings: bind command");
        check("bind DPAD_UP \"+actionslot 1\"".equals(SecondScreenBindings.command("DPAD_UP", "+actionslot 1")), "bindings: quoted argument");
        check("unbind AUX3".equals(SecondScreenBindings.command("AUX3", "")), "bindings: unbind");
        check(SecondScreenBindings.command("AUX1", "+attack;quit").indexOf(';') < 0, "bindings: no command separators");
        Map<String, String> parsed = SecondScreenBindings.parse("AUX1=+gostand\nBUTTON_A=\nDPAD_UP=+actionslot 1\n");
        check("+gostand".equals(parsed.get("AUX1")) && "".equals(parsed.get("BUTTON_A")) && "+actionslot 1".equals(parsed.get("DPAD_UP")),
            "bindings: parse the engine's reply");
        check("Fire".equals(SecondScreenBindings.label("+attack")) && "+custom".equals(SecondScreenBindings.label("+custom")),
            "bindings: labels");
        for (SecondScreenBindings.Action a : SecondScreenBindings.ACTIONS) {
            check(a.command.indexOf(';') < 0 && a.command.indexOf('"') < 0, "bindings: action " + a.label + " is one command");
        }
    }
}
