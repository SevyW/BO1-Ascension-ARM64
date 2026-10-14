#!/bin/bash
# build-p32.sh [errors] [files]: regenerate the 32-bit-pointer tree from /bo1/src and build it as 64-bit x86
# (inside the bo1-x64 container) with the patched clang in /bo1/llvm/build (tools/p32/clang-21-x86_64-ptr32-abi.patch).
# REGEN=0 skips the regeneration (build only).
cd /bo1/src || exit 1
# DXVK=<dxvk source tree> adds the Vulkan renderer (default /bo1/dxvk/src when it exists)
DXVK=${DXVK-$([ -d /bo1/dxvk/src/include/native ] && echo /bo1/dxvk/src)}
# FFMPEG=<FFmpeg build> adds the xWMA sounds (default /bo1/ffmpeg/linux when it exists, build-ffmpeg.sh)
FFMPEG=${FFMPEG-$([ -f /bo1/ffmpeg/linux/lib/libavcodec.a ] && echo /bo1/ffmpeg/linux)}
DB=build/native-x64
if [ "${REGEN:-1}" = 1 ]; then
  [ -f $DB/compile_commands.json ] || cmake -S . -B $DB -G Ninja -DBO1_NATIVE=ON -DBO1_NATIVE_ARCH=x64 -DBO1_NATIVE_DXVK=$DXVK -DBO1_NATIVE_FFMPEG=$FFMPEG -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > $DB.configure.log 2>&1
  [ -x $DB/p32rewrite ] && [ $DB/p32rewrite -nt tools/p32/p32rewrite.cpp ] || clang++-21 -std=c++17 -O1 -fno-rtti tools/p32/p32rewrite.cpp -o $DB/p32rewrite $(llvm-config-21 --cxxflags | sed "s/-std=[^ ]*//") -I/usr/lib/llvm-21/include -L/usr/lib/llvm-21/lib -lclang-cpp -lLLVM-21 || exit 1
  # keep the previous build directory: only files whose text changed get new timestamps
  rm -rf /bo1/p32new
  nice -n 19 ionice -c3 python3 tools/p32/p32tree.py --db $DB --out /bo1/p32new --jobs ${JOBS:-6} | grep -E "pointers,"
  mkdir -p /bo1/p32tree
  rsync -a --checksum --delete --exclude build /bo1/p32new/ /bo1/p32tree/
  rm -rf /bo1/p32new
fi
cd /bo1/p32tree || exit 1
B=build/x64; mkdir -p build
[ -f $B/build.ninja ] || cmake -S . -B $B -G Ninja -DBO1_NATIVE=ON -DBO1_NATIVE_ARCH=x64 -DBO1_NATIVE_DXVK=$DXVK -DBO1_NATIVE_FFMPEG=$FFMPEG -DCMAKE_C_COMPILER=/bo1/llvm/build/bin/clang -DCMAKE_CXX_COMPILER=/bo1/llvm/build/bin/clang++ "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem /usr/lib/llvm-21/include/c++/v1" "-DCMAKE_EXE_LINKER_FLAGS=-L/usr/lib/llvm-21/lib -Wl,-rpath,/usr/lib/llvm-21/lib" "-DCMAKE_CXX_STANDARD_LIBRARIES=-lc++abi" -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Release} > $B.configure.log 2>&1 || { tail -20 $B.configure.log; exit 1; }
nice -n 19 ionice -c3 ninja -C $B -j ${JOBS:-6} -k 0 > $B/build.log 2>&1
echo "ninja exit=$?  failed TUs: $(grep -c "^FAILED" $B/build.log)  errors: $(grep -c "error:" $B/build.log)"
grep -E "error:" $B/build.log | sed -E "s/^[^ ]+: //" | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -${1:-40}
echo "--- by file"
grep -E "error:" $B/build.log | cut -d: -f1 | sed "s#/bo1/p32tree/##" | sort | uniq -c | sort -rn | head -${2:-25}
