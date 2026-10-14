#!/bin/bash
# build-llvm.sh: the patched clang 21.1.8 the 32-bit-pointer builds need (/bo1/llvm/build/bin/clang), inside the
# build container. The patches in tools/p32 fix two __ptr32 code generation bugs of clang 21 (x86-64 call ABI,
# AArch64 truncating stores); the NDK's clang has them too, so Android is compiled with this one. About an hour on
# 6 cores; JOBS (default 6) sets the parallel jobs.
L=/bo1/llvm; V=21.1.8; mkdir -p $L; cd $L || exit 1
[ -f llvm-project-$V.src.tar.xz ] || curl -sSfL -o llvm-project-$V.src.tar.xz \
  https://github.com/llvm/llvm-project/releases/download/llvmorg-$V/llvm-project-$V.src.tar.xz || exit 1
if [ ! -d llvm-project-$V.src ]; then
  tar xf llvm-project-$V.src.tar.xz || exit 1
  for p in /bo1/src/tools/p32/clang-21-x86_64-ptr32-abi.patch /bo1/src/tools/p32/clang-21-aarch64-ptr32-truncstore.patch; do
    patch -d llvm-project-$V.src -p1 < $p || exit 1
  done
fi
cmake -S llvm-project-$V.src/llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
  "-DLLVM_TARGETS_TO_BUILD=X86;AArch64" -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DCLANG_INCLUDE_TESTS=OFF -DLLVM_ENABLE_ASSERTIONS=OFF -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DLLVM_USE_LINKER=lld -DLLVM_PARALLEL_LINK_JOBS=2 -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF \
  > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
nice -n 19 ninja -C build -j ${JOBS:-6} clang clang-resource-headers > build.log 2>&1 || { tail -20 build.log; exit 1; }
build/bin/clang --version | head -1
