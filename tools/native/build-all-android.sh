#!/bin/bash
# build-all-android.sh: everything from a fresh build container (tools/native/docker/Dockerfile) to the APK in
# /bo1/android/out. Steps that are done already are skipped by the scripts themselves. Takes hours the first time
# (clang, DXVK, FFmpeg, the engine twice); later runs rebuild what changed.
set -e
S=/bo1/src/tools/native
$S/get-ndk.sh
[ -x /bo1/llvm/build/bin/clang ] || $S/build-llvm.sh
$S/android-setup.sh
$S/build-dxvk.sh linux
$S/build-ffmpeg.sh linux
$S/build-ffmpeg.sh arm64-v8a
$S/build-p32.sh
$S/build-android.sh          # also SDL2 for Android, which DXVK's Android build links
$S/build-dxvk.sh arm64-v8a
$S/build-adrenotools.sh       # mod (turnip): libadrenotools, for a custom Vulkan driver
$S/build-apk.sh
