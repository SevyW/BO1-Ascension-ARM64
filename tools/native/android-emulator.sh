#!/bin/bash
# android-emulator.sh start|stop|shot <name>|log: the virtual device of android-emulator-setup.sh, headless, inside
# the bo1-emu container (KVM). adb is $ADB below; screenshots go to /bo1/home/shots.
export ANDROID_SDK_ROOT=/bo1/android/sdk ANDROID_AVD_HOME=/bo1/android/avd HOME=/root
ADB=$ANDROID_SDK_ROOT/platform-tools/adb
case "$1" in
start)
  if ! $ADB devices 2>/dev/null | grep -q "emulator-.*device"; then
    nohup nice -n 10 $ANDROID_SDK_ROOT/emulator/emulator -avd bo1 -no-window -no-audio -no-boot-anim -no-snapshot \
      -gpu swiftshader_indirect -memory 6144 -cores 6 -no-metrics > /bo1/android/emulator.log 2>&1 &
  fi
  $ADB wait-for-device
  for i in $(seq 1 120); do
    [ "$($ADB shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break
    sleep 2
  done
  echo "boot_completed=$($ADB shell getprop sys.boot_completed | tr -d '\r') sdk=$($ADB shell getprop ro.build.version.sdk | tr -d '\r') abi=$($ADB shell getprop ro.product.cpu.abi | tr -d '\r')"
  ;;
stop)
  $ADB emu kill
  ;;
shot)
  mkdir -p /bo1/home/shots
  $ADB exec-out screencap -p > /bo1/home/shots/$2.png; ls -la /bo1/home/shots/$2.png
  ;;
log)
  $ADB shell "cat /sdcard/Android/data/org.bo1port.zombies/files/bo1.log" 2>&1
  ;;
*)
  shift 0; $ADB "$@"
  ;;
esac
