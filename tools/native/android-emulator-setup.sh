#!/bin/bash
# android-emulator-setup.sh: an x86-64 Android 14 virtual device for trying the app on the build host (the emulator
# cannot run arm64 images on an x86-64 host, so the app is also built for x86_64: build-android.sh x86_64).
# Run inside the bo1-x64 container (it has the JDK); the device itself runs in bo1-emu, a container with /dev/kvm:
#   docker run -d --name bo1-emu --device /dev/kvm --cpus 8 --memory 12g -v /root/bo1:/bo1 bo1-build-x64 sleep infinity
A=/bo1/android; SDK=$A/sdk
export ANDROID_SDK_ROOT=$SDK ANDROID_AVD_HOME=$A/avd
mkdir -p $ANDROID_AVD_HOME
SM="$SDK/cmdline-tools/latest/bin/sdkmanager --sdk_root=$SDK"
yes | $SM "system-images;android-34;default;x86_64" "emulator" >> $A/sdkmanager.log 2>&1; echo "system image: $?"
[ -d $ANDROID_AVD_HOME/bo1.avd ] || echo no | $SDK/cmdline-tools/latest/bin/avdmanager create avd -n bo1 -k "system-images;android-34;default;x86_64" -d pixel_5 > $A/avd.log 2>&1
echo "avd: $?"; ls $ANDROID_AVD_HOME
# a data partition that holds the game files, and the host GPU off (software GLES 3 through SwiftShader)
CFG=$ANDROID_AVD_HOME/bo1.avd/config.ini
sed -i '/^disk.dataPartition.size/d;/^hw.ramSize/d;/^hw.gpu/d;/^hw.keyboard=/d;/^vm.heapSize/d' $CFG
cat >> $CFG <<EOF
disk.dataPartition.size=32G
hw.ramSize=6144
hw.gpu.enabled=yes
hw.gpu.mode=swiftshader_indirect
hw.keyboard=yes
EOF
echo EMU-SETUP-DONE
