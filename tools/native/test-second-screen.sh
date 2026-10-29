#!/bin/bash
# test-second-screen.sh: the second-screen controller's maths and tables (android/java/.../SecondScreenMath.java,
# SecondScreenBindings.java) tested on the build host with a plain JDK: layout, joystick, radial slices, d-pad
# zones, scroll wheel, click detection, trackball inertia, look gain, bind commands. No device or Android SDK needed.
R=$(cd "$(dirname "$0")/../.." && pwd)
P=$R/android/java/org/bo1port/zombies
OUT=$(mktemp -d)
javac -nowarn -d "$OUT" "$P/SecondScreenMath.java" "$P/SecondScreenBindings.java" \
  "$R/android/test/org/bo1port/zombies/SecondScreenMathTest.java" || exit 1
java -cp "$OUT" org.bo1port.zombies.SecondScreenMathTest
status=$?
rm -rf "$OUT"
exit $status
