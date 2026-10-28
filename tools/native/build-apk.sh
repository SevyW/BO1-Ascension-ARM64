#!/bin/bash
# build-apk.sh: package the Android build (build-android.sh) as /bo1/android/out/bo1-zombies-debug.apk, without
# Gradle: javac + d8 for SDL's Java side and Bo1Activity, aapt2 for the manifest, zip for the native libraries,
# zipalign (16 KB pages) and apksigner with a debug key made on first use. Runs inside the bo1-x64 container.
# No game data goes into the package: the game files are copied to the device separately (see bo1_boot.c).
# Release signing: RELEASE_KEYSTORE=<file> (alias RELEASE_ALIAS, default bo1port) and the password in the file
# RELEASE_PASS_FILE give /bo1/android/out/bo1-port-<versionName>.apk. Keep the keystore out of the repository: every
# update has to be signed with the same key, or Android refuses to install it over the previous one.
A=/bo1/android
SDK=$A/sdk; BT=$SDK/build-tools/35.0.0; JAR=$SDK/platforms/android-35/android.jar
SRC=/bo1/src/android
NB=/bo1/p32tree/build/android/bin
NDK=/bo1/ndk/ndk-r30/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64
W=$A/apk-work; OUT=$A/out
rm -rf $W; mkdir -p $W/classes $W/dex $W/pkg/lib/arm64-v8a $OUT

# Java: SDL's activity and ours
find $A/SDL2/android-project/app/src/main/java $SRC/java -name "*.java" > $W/sources.txt
javac -nowarn -source 8 -target 8 -bootclasspath $JAR:$BT/core-lambda-stubs.jar -d $W/classes @$W/sources.txt 2> $W/javac.log \
  || { grep -v "^warning\|^Note" $W/javac.log | head -20; exit 1; }
$BT/d8 --release --min-api 26 --lib $JAR --output $W/dex $(find $W/classes -name "*.class") > $W/d8.log 2>&1 || { tail -20 $W/d8.log; exit 1; }

# manifest -> base package
# The launcher icon is not in the repository (the repository holds no game art). ICON_DIR (default
# /bo1/android/icon) may hold
#   icon-192.png      the plain icon (192x192), for launchers without adaptive icons
#   icon-bg-432.png   the adaptive icon's background layer (432x432: 108dp, of which the launcher shows the middle
#   icon-fg-432.png   72dp behind its own mask) and its foreground layer (may be fully transparent)
# With only the plain icon a current launcher puts it, shrunk, on a white disc. Without any the app has Android's
# default icon.
ICON_DIR=${ICON_DIR:-$A/icon}
MANIFEST=$SRC/AndroidManifest.xml; RES=""
if [ -f $ICON_DIR/icon-192.png ]; then
  mkdir -p $W/res/mipmap-xxxhdpi && cp $ICON_DIR/icon-192.png $W/res/mipmap-xxxhdpi/ic_launcher.png
  if [ -f $ICON_DIR/icon-bg-432.png ] && [ -f $ICON_DIR/icon-fg-432.png ]; then
    cp $ICON_DIR/icon-bg-432.png $W/res/mipmap-xxxhdpi/ic_launcher_background.png
    cp $ICON_DIR/icon-fg-432.png $W/res/mipmap-xxxhdpi/ic_launcher_foreground.png
    mkdir -p $W/res/mipmap-anydpi-v26
    cat > $W/res/mipmap-anydpi-v26/ic_launcher.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
EOF
  fi
  $BT/aapt2 compile --dir $W/res -o $W/res.zip > $W/aapt2-compile.log 2>&1 || { tail -20 $W/aapt2-compile.log; exit 1; }
  sed 's#<application#<application android:icon="@mipmap/ic_launcher"#' $SRC/AndroidManifest.xml > $W/AndroidManifest.xml
  MANIFEST=$W/AndroidManifest.xml; RES=$W/res.zip
fi
# licences of what the package contains (THIRD_PARTY_NOTICES.md), as assets/licenses/
L=$W/assets/licenses; mkdir -p $L
cp /bo1/src/NOTICE.md /bo1/src/THIRD_PARTY_NOTICES.md $L/ || exit 1
cp /bo1/src/third_party/mojoshader/LICENSE.txt $L/mojoshader.txt; cp /bo1/src/third_party/sse2neon/LICENSE $L/sse2neon.txt
cp /bo1/src/src/libs/libtommath-1.0/LICENSE $L/libtommath.txt; cp /bo1/src/src/jpeg/README $L/libjpeg.txt
for f in /bo1/dxvk/src/LICENSE:dxvk.txt $A/SDL2/LICENSE.txt:sdl2.txt /bo1/ffmpeg/src/COPYING.LGPLv2.1:ffmpeg-lgpl-2.1.txt \
    /bo1/adrenotools/src/LICENSE:libadrenotools.txt; do
  cp ${f%%:*} $L/${f##*:} || exit 1
done
$BT/aapt2 link -o $W/base.apk --manifest $MANIFEST -I $JAR $RES -A $W/assets > $W/aapt2.log 2>&1 || { tail -20 $W/aapt2.log; exit 1; }

# native libraries, without debug information (the unstripped libbo1.so stays in the build directory for symbols)
for lib in libbo1.so libmain.so; do $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/arm64-v8a/$lib $NB/$lib || exit 1; done
$NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/arm64-v8a/libSDL2.so $A/sdl/lib/libSDL2.so || exit 1
# the Vulkan renderer: DXVK (build-dxvk.sh), when it was built
DX=/bo1/dxvk/build-android-arm64-v8a/src/d3d9/libdxvk_d3d9.so
[ -f $DX ] && { $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/arm64-v8a/libdxvk_d3d9.so $DX || exit 1; }
# mod (turnip): libadrenotools and its hook libraries (build-adrenotools.sh), for a custom Vulkan driver (bo1_driver.c)
AT=/bo1/adrenotools/build
for lib in $AT/libadrenotools.so $AT/src/hook/libhook_impl.so $AT/src/hook/libmain_hook.so \
    $AT/src/hook/libfile_redirect_hook.so $AT/src/hook/libgsl_alloc_hook.so; do
  $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/arm64-v8a/$(basename $lib) $lib || exit 1
done
# X86=1: also the x86_64 build (ABI=x86_64 build-android.sh), for the emulator
if [ "${X86:-0}" = 1 ]; then
  XB=/bo1/p32tree/build/android-x86_64/bin; mkdir -p $W/pkg/lib/x86_64
  for lib in libbo1.so libmain.so; do $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/x86_64/$lib $XB/$lib || exit 1; done
  $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/x86_64/libSDL2.so $A/sdl-x86_64/lib/libSDL2.so || exit 1
  DX=/bo1/dxvk/build-android-x86_64/src/d3d9/libdxvk_d3d9.so
  [ -f $DX ] && { $NDK/bin/llvm-strip --strip-unneeded -o $W/pkg/lib/x86_64/libdxvk_d3d9.so $DX || exit 1; }
fi
cp $W/dex/classes.dex $W/pkg/
(cd $W/pkg && zip -q -r ../base.apk classes.dex lib) || exit 1

$BT/zipalign -f -P 16 4 $W/base.apk $W/aligned.apk || exit 1
if [ -n "$RELEASE_KEYSTORE" ]; then
  VER=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' $SRC/AndroidManifest.xml)
  NAME=bo1-port-$VER
  $BT/apksigner sign --ks "$RELEASE_KEYSTORE" --ks-key-alias ${RELEASE_ALIAS:-bo1port} --ks-pass file:"$RELEASE_PASS_FILE"     --out $OUT/$NAME.apk $W/aligned.apk || exit 1
  $BT/apksigner verify --print-certs $OUT/$NAME.apk | head -3
  $BT/aapt2 dump badging $OUT/$NAME.apk | grep -E "^package|sdkVersion|native-code"
  sha256sum $OUT/$NAME.apk
  exit 0
fi
[ -f $A/debug.keystore ] || keytool -genkeypair -keystore $A/debug.keystore -storepass android -keypass android -alias androiddebugkey \
  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" > /dev/null 2>&1
NAME=bo1-zombies-turnip-debug; [ "${X86:-0}" = 1 ] && NAME=bo1-zombies-turnip-debug-emulator
$BT/apksigner sign --ks $A/debug.keystore --ks-pass pass:android --key-pass pass:android --out $OUT/$NAME.apk $W/aligned.apk || exit 1
$BT/apksigner verify --verbose $OUT/$NAME.apk | head -4
$BT/aapt2 dump badging $OUT/$NAME.apk | grep -E "^package|sdkVersion|native-code|launchable"
unzip -l $OUT/$NAME.apk | tail -8
ls -la $OUT/$NAME.apk
