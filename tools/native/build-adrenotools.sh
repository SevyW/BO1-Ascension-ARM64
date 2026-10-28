#!/bin/bash
# build-adrenotools.sh: libadrenotools (github.com/bylaws/libadrenotools, BSD 2-clause) for Android arm64, with the
# NDK's clang, inside the bo1-x64 container. It loads a custom Vulkan driver (Mesa Turnip) behind a private copy of
# the system's Vulkan loader (src/native/android/bo1_driver.c). build-apk.sh packs what it makes into lib/arm64-v8a:
#   libadrenotools.so, and the hook libraries it loads from the same directory: libhook_impl.so, libmain_hook.so,
#   libfile_redirect_hook.so, libgsl_alloc_hook.so            -> /bo1/adrenotools/build
T=/bo1/adrenotools; REV=8fae8ce254dfc1344527e05301e43f37dea2df80
NDK=/bo1/ndk/ndk-r30/android-ndk-r30
if [ ! -d $T/src ]; then
  git clone https://github.com/bylaws/libadrenotools $T/src && git -C $T/src checkout -q $REV \
    && git -C $T/src submodule update -q --init --recursive || exit 1
fi
cmake -S $T/src -B $T/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384" > $T/configure.log 2>&1 || { tail -20 $T/configure.log; exit 1; }
nice -n 19 ninja -C $T/build -j ${JOBS:-6} > $T/build.log 2>&1 || { grep -m5 -A8 "error:" $T/build.log; exit 1; }
find $T/build -name "lib*.so" -exec ls -la {} \;
