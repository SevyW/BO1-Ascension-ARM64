#!/bin/bash
# get-ndk.sh: Android NDK r30 in /bo1/ndk/ndk-r30/android-ndk-r30, where the Android scripts expect it.
N=/bo1/ndk; mkdir -p $N/ndk-r30; cd $N || exit 1
[ -f ndk-r30.zip ] || curl -sSfL -o ndk-r30.zip https://dl.google.com/android/repository/android-ndk-r30-linux.zip || exit 1
[ -d ndk-r30/android-ndk-r30 ] || unzip -q ndk-r30.zip -d ndk-r30 || exit 1
ls ndk-r30/android-ndk-r30/source.properties && grep Pkg.Revision ndk-r30/android-ndk-r30/source.properties
