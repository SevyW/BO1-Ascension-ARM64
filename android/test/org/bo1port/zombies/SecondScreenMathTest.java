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
        click();
        press();
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
            SecondScreenMath.Box[] all = {l.hide, l.remap, l.pad};
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
            check(l.pad.width() * l.pad.height() > size[0] * size[1] * 0.8f, name + ": the pad fills most of the screen");
        }
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

    /** Feeds a touch's sizes (raw units, one sample every stepMs from t0); returns the press changes in order. */
    private static String feed(SecondScreenMath.Press p, long t0, long stepMs, int... sizes) {
        StringBuilder out = new StringBuilder();
        p.down(t0, sizes[0]);
        for (int k = 1; k < sizes.length; ++k) {
            final int c = p.size(t0 + k * stepMs, sizes[k]);
            if (c != 0) {
                out.append(c > 0 ? 'P' : 'R');
            }
        }
        return out.toString();
    }

    static void press() {
        // measured on the Thor: a light finger 2..5, a thumb pressed flat 11..13, rising 4,5,6,7,8.. in 0.2-0.3 s
        check("".equals(feed(new SecondScreenMath.Press(), 0, 40, 3, 4, 3, 2, 3, 4, 5, 4, 3, 4, 5, 3, 2)),
            "press: a light finger sliding (2..5) never clicks");
        check("P".equals(feed(new SecondScreenMath.Press(), 0, 30, 4, 4, 5, 6, 7, 8, 10, 12, 13, 12, 13)),
            "press: 4 -> 13 within 0.3 s clicks");
        check("PR".equals(feed(new SecondScreenMath.Press(), 0, 30, 4, 5, 8, 11, 13, 12, 9, 6, 4)),
            "press: flatten then relax = press and release");
        check("PRP".equals(feed(new SecondScreenMath.Press(), 0, 30, 4, 7, 10, 12, 12, 8, 5, 4, 7, 10, 12)),
            "press: pressing again without lifting clicks again");
        check("".equals(feed(new SecondScreenMath.Press(), 0, 200, 4, 5, 6, 7, 8, 9, 10, 11, 12)),
            "press: a slow roll (1.6 s from 4 to 12) is no click");
        SecondScreenMath.Press slow = new SecondScreenMath.Press();
        feed(slow, 0, 200, 4, 6, 8, 10, 12);
        check(slow.relaxed() >= 10, "press: after a slow roll the flatter finger is the new relaxed size (" + slow.relaxed() + ")");
        SecondScreenMath.Press deep = new SecondScreenMath.Press();
        deep.minRise = 9f;
        check("".equals(feed(deep, 0, 30, 4, 6, 8, 10, 12)), "press: a deeper setting needs more (4 -> 12 is not 9 more)");
        check("P".equals(feed(deep, 0, 30, 3, 6, 9, 12, 13)), "press: ... 3 -> 13 is");

        // the look pauses while the finger flattens, and briefly around a press, then aiming while firing works
        SecondScreenMath.Press f = new SecondScreenMath.Press();
        f.down(0, 4);
        check(!f.frozen(10, 4), "freeze: a resting finger aims");
        check(!f.frozen(20, 5), "freeze: light-touch noise aims");
        f.size(40, 7);
        check(f.frozen(40, 7.5f), "freeze: a fast climb toward a press holds the view");
        check(f.size(80, 12) == 1, "freeze: the press");
        check(f.frozen(150, 12), "freeze: just after the press");
        check(!f.frozen(260, 12), "freeze: then the view follows the finger while firing");
        check(f.size(300, 6) == -1, "freeze: release");
        check(f.frozen(350, 6), "freeze: just after the release");
        check(f.up(400) == false, "press: lifting after a release ends nothing");
        SecondScreenMath.Press u = new SecondScreenMath.Press();
        feed(u, 0, 30, 4, 8, 12);
        check(u.pressed() && u.up(200) && !u.pressed(), "press: lifting while pressed ends the press");
    }

    static void gain() {
        near(SecondScreenMath.lookGain(0, 1.5f, 0.5f), 1.5f, 1e-6f, "gain: at rest the sensitivity");
        near(SecondScreenMath.lookGain(1000, 1f, 0.5f), 1.5f, 1e-6f, "gain: 1000 px/s with accel 0.5 = 1.5x");
        near(SecondScreenMath.lookGain(100000, 1f, 1f), 4f, 1e-6f, "gain: capped at 3x extra");
        near(SecondScreenMath.lookGain(5000, 2f, 0f), 2f, 1e-6f, "gain: no acceleration, flat");
    }

    static void bindings() {
        List<SecondScreenBindings.Input> all = SecondScreenBindings.all();
        check(all.size() == 16 + 1, "bindings: 17 inputs (" + all.size() + ")");
        java.util.HashSet<String> keys = new java.util.HashSet<String>();
        java.util.HashSet<Integer> codes = new java.util.HashSet<Integer>();
        for (SecondScreenBindings.Input i : all) {
            check(keys.add(i.key), "bindings: key " + i.key + " once");
            if (i.code != 0) {
                check(codes.add(i.code), "bindings: code " + i.code + " once");
            }
        }
        // key codes as src/client/cl_keys.cpp names them
        check(SecondScreenBindings.PAD_PRESS.code == 212 && "AUX6".equals(SecondScreenBindings.PAD_PRESS.key), "bindings: AUX6 = 212");
        Map<String, String> d = SecondScreenBindings.consoleDefault();
        for (SecondScreenBindings.Input i : all) {
            check(d.containsKey(i.key), "bindings: the default preset binds " + i.key);
        }
        check("+gostand".equals(d.get("BUTTON_A")) && "+attack".equals(d.get("BUTTON_RTRIG")), "bindings: console default A / RT");
        check("+attack".equals(d.get("AUX6")), "bindings: the trackpad press fires by default");
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
