#!/bin/bash
# android-setup.sh: what the Android build needs besides the NDK (already in /bo1/ndk), inside the bo1-x64 container:
# a JDK, the Android SDK build tools and platform (aapt2, d8, apksigner, android.jar), the SDL2 source, and an
# arm64 system image whose bionic (linker64, libc) lets the engine library be tried under qemu-user.
export DEBIAN_FRONTEND=noninteractive
A=/bo1/android; mkdir -p $A/dl $A/sdk; cd $A || exit 1
apt-get install -y -qq --no-install-recommends openjdk-21-jdk-headless curl p7zip-full e2fsprogs erofs-utils zip > $A/apt.log 2>&1
echo "java: $(java -version 2>&1 | head -1)"
cd $A/dl
[ -f cmdline-tools.zip ] || curl -sSfL -o cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
if [ ! -x $A/sdk/cmdline-tools/latest/bin/sdkmanager ]; then
  rm -rf $A/sdk/cmdline-tools; mkdir -p $A/sdk/cmdline-tools; unzip -q cmdline-tools.zip -d $A/sdk/cmdline-tools && mv $A/sdk/cmdline-tools/cmdline-tools $A/sdk/cmdline-tools/latest
fi
SM="$A/sdk/cmdline-tools/latest/bin/sdkmanager --sdk_root=$A/sdk"
yes | $SM --licenses > $A/licenses.log 2>&1
$SM "build-tools;35.0.0" "platforms;android-35" "platform-tools" > $A/sdkmanager.log 2>&1; echo "sdk tools: $?"
$SM "system-images;android-34;default;arm64-v8a" >> $A/sdkmanager.log 2>&1; echo "system image: $?"
for v in 2.32.10 2.32.8 2.32.6 2.30.12; do
  [ -d $A/SDL2 ] && break
  curl -sSfL -o SDL2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$v/SDL2-$v.tar.gz && tar xf SDL2.tar.gz -C $A && mv $A/SDL2-$v $A/SDL2 && echo "SDL2 $v"
done
ls $A $A/sdk $A/sdk/build-tools 2>&1 | tr "\n" " "; echo
du -sh $A/sdk/system-images 2>/dev/null
echo SETUP-DONE
