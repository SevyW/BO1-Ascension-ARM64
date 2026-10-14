#!/bin/bash
# build-ffmpeg.sh [linux|arm64-v8a|x86_64]: FFmpeg's libavcodec and libavutil with one decoder, WMA version 2, as
# static libraries for the native mixer (src/native/native_audio.cpp). Runs inside the bo1-x64 container.
#   linux      -> /bo1/ffmpeg/linux/{include,lib}
#   arm64-v8a  -> /bo1/ffmpeg/android-arm64-v8a/...   (the NDK's clang)
#   x86_64     -> /bo1/ffmpeg/android-x86_64/...      (for the emulator)
# Why: on PC most of the game's sound effects (weapons, explosions, footsteps, ambience) are xWMA (WMA 2 packets
# with a table of decoded sizes); only a few sounds are PCM or MS-ADPCM. Without a decoder they are silent.
# Source: FFmpeg n7.1 (LGPL 2.1 or later with this configuration: no --enable-gpl, no --enable-nonfree), cloned
# on first use. Needs make (apt-get install make). Pass the directory to cmake as -DBO1_NATIVE_FFMPEG=<dir> (build-p32.sh and build-android.sh do).
F=/bo1/ffmpeg; VER=n7.1
TARGET=${1:-linux}
NDK=/bo1/ndk/ndk-r30/android-ndk-r30
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
API=26
[ -d $F/src ] || git clone --depth 1 -b $VER https://github.com/FFmpeg/FFmpeg $F/src || exit 1
COMMON="--disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avformat --disable-swscale
  --disable-swresample --disable-avfilter --disable-network --disable-autodetect --enable-decoder=wmav2
  --enable-static --disable-shared --enable-pic --disable-debug"
if [ "$TARGET" = linux ]; then
  OUT=$F/linux
  EXTRA="--cc=clang-21 --disable-x86asm"
else
  if [ "$TARGET" = x86_64 ]; then
    TRIPLE=x86_64-linux-android$API; ARCH=x86_64; EXTRA="--disable-x86asm"
  else
    TARGET=arm64-v8a; TRIPLE=aarch64-linux-android$API; ARCH=aarch64; EXTRA=""
  fi
  OUT=$F/android-$TARGET
  EXTRA="$EXTRA --host-cc=clang-21 --enable-cross-compile --target-os=android --arch=$ARCH --cc=$TC/bin/$TRIPLE-clang --ar=$TC/bin/llvm-ar
    --nm=$TC/bin/llvm-nm --ranlib=$TC/bin/llvm-ranlib --strip=$TC/bin/llvm-strip"
fi
B=$F/build-$(basename $OUT)
mkdir -p $B && cd $B || exit 1
[ -f config.h ] || $F/src/configure --prefix=$OUT $COMMON $EXTRA > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
nice -n 19 ionice -c3 make -j${JOBS:-6} > build.log 2>&1 && make install > install.log 2>&1
echo "make exit=$?  errors: $(grep -c "error:" build.log)"
grep -E "error:" -A3 build.log | head -20
ls -la $OUT/lib/*.a 2>/dev/null
