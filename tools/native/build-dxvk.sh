#!/bin/bash
# build-dxvk.sh [linux|arm64-v8a|x86_64]: DXVK's native Direct3D 9 library (libdxvk_d3d9.so, SDL2 window system) for
# the Vulkan renderer (src/native/vk, src/web/d3d9/d3d9_vkbridge.cpp). Runs inside the bo1-x64 container.
#   linux      -> /bo1/dxvk/build-linux/src/d3d9/libdxvk_d3d9.so          (the host's clang and SDL2)
#   arm64-v8a  -> /bo1/dxvk/build-android-arm64-v8a/src/d3d9/libdxvk_d3d9.so   (the NDK's clang, build-android.sh's SDL2)
#   x86_64     -> /bo1/dxvk/build-android-x86_64/...                       (for the emulator)
# DXVK is an ordinary 64-bit library: it is not built with the patched clang and not rewritten (tools/p32).
# Source: DXVK v2.6.1 (zlib licence), cloned on first use. Two changes for Android: the SDL2 library's name, and
# the presenter keeps a swap chain that Android calls suboptimal.
D=/bo1/dxvk; VER=v2.6.1
TARGET=${1:-linux}
NDK=/bo1/ndk/ndk-r30/android-ndk-r30
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
API=26
A=/bo1/android

if [ ! -d $D/src ]; then
  git clone --recursive --depth 1 --shallow-submodules -b $VER https://github.com/doitsujin/dxvk $D/src || exit 1
fi
# Android's SDL2 is libSDL2.so (no version in the name); the app has it loaded already
W=$D/src/src/wsi/sdl2/wsi_platform_sdl2.cpp
grep -q __ANDROID__ $W || python3 - "$W" <<'EOF' || exit 1
import sys
p = sys.argv[1]
s = open(p).read()
a = '#elif defined(__APPLE__)\n        "libSDL2-2.0.0.dylib"\n'
assert s.count(a) == 1
s = s.replace(a, a + '#elif defined(__ANDROID__)\n        "libSDL2.so"\n')
open(p, "w").write(s)
EOF

# Android says VK_SUBOPTIMAL_KHR for every image of a surface whose transform is not the identity (a display that is
# portrait by nature, held in landscape: an AYN Odin 3). DXVK would make a new swap chain for every frame; on the
# Adreno 830 that ended in a crash after 402 of them. The system's compositor rotates the picture either way.
P=$D/src/src/dxvk/dxvk_presenter.cpp
grep -q BO1_STATUS $P || python3 - "$P" <<'EOF' || exit 1
import sys
p = sys.argv[1]
s = open(p).read()
a = "namespace dxvk {\n"
i = s.index(a)
s = s[:i] + """#ifdef __ANDROID__
// BO1: see tools/native/build-dxvk.sh
namespace {
  struct Bo1Status { VkResult operator << (VkResult r) const { return r == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : r; } };
  const Bo1Status bo1Status;
}
#define BO1_STATUS bo1Status <<
#else
#define BO1_STATUS
#endif

""" + s[i:]
n = s.count("m_acquireStatus = m_vkd->vkAcquireNextImageKHR(")
assert n == 3, n
s = s.replace("m_acquireStatus = m_vkd->vkAcquireNextImageKHR(", "m_acquireStatus = BO1_STATUS m_vkd->vkAcquireNextImageKHR(")
a = "VkResult status = m_vkd->vkQueuePresentKHR("
assert s.count(a) == 1
s = s.replace(a, "VkResult status = BO1_STATUS m_vkd->vkQueuePresentKHR(")
open(p, "w").write(s)
EOF

# Pipelines in the background. A driver without VK_EXT_graphics_pipeline_library (Adreno 830, driver 0.800) gives
# DXVK no fast way to make a pipeline: the first draw with a new shader and state combination waited for the driver's
# compiler, which is the stutter of the first minutes on every map. With this change such a pipeline is compiled by
# DXVK's worker threads and the draws that need it are left out until it is ready (a few frames, once per
# combination). BO1_DXVK_ASYNC=0 in the environment restores the waiting.
G=$D/src/src/dxvk/dxvk_graphics.cpp
grep -q BO1_ASYNC $G || python3 - "$G" <<'EOF' || exit 1
import sys
p = sys.argv[1]
s = open(p).read()
old = """        bool canCreateBasePipeline = this->canCreateBasePipeline(state);
        instance = this->createInstance(state, canCreateBasePipeline);

        // Unlock here since we may dispatch the pipeline to a worker,
        // which will then acquire it to increment the use counter.
        lock.unlock();

        // If necessary, compile an optimized pipeline variant
        if (!instance->fastHandle.load())
          m_workers->compileGraphicsPipeline(this, state, DxvkPipelinePriority::Low);
"""
new = """        bool canCreateBasePipeline = this->canCreateBasePipeline(state);
        // BO1_ASYNC: see tools/native/build-dxvk.sh
        bool bo1Async = !canCreateBasePipeline && bo1AsyncPipelines();

        if (bo1Async) {
          m_stats->numGraphicsPipelines += 1;
          instance = &(*m_pipelines.emplace(state, VkPipeline(VK_NULL_HANDLE),
            VkPipeline(VK_NULL_HANDLE), computeAttachmentMask(state)));
        } else {
          instance = this->createInstance(state, canCreateBasePipeline);
        }

        // Unlock here since we may dispatch the pipeline to a worker,
        // which will then acquire it to increment the use counter.
        lock.unlock();

        // If necessary, compile an optimized pipeline variant
        if (bo1Async)
          m_workers->compileGraphicsPipeline(this, state, DxvkPipelinePriority::High);
        else if (!instance->fastHandle.load())
          m_workers->compileGraphicsPipeline(this, state, DxvkPipelinePriority::Low);
"""
assert s.count(old) == 1
s = s.replace(old, new)
a = "namespace dxvk {\n"
i = s.index(a) + len(a)
s = s[:i] + """
  // BO1_ASYNC: see tools/native/build-dxvk.sh
  static bool bo1AsyncPipelines() {
    static int enabled = -1;
    if (enabled < 0) {
      const char* env = std::getenv("BO1_DXVK_ASYNC");
      enabled = env ? std::atoi(env) != 0 : 1;
    }
    return enabled != 0;
  }
""" + s[i:]
if "#include <cstdlib>" not in s:
    s = "#include <cstdlib>\n" + s
open(p, "w").write(s)
EOF

# How a multisample resolve is done (StretchRect from the 4x back buffer). BO1_DXVK_RESOLVE in the environment:
#   0  DXVK's choice: folded into the render pass that is open on the source, else a pass of its own
#   1  always a pass of its own with a resolve attachment
#   2  a fragment shader that reads the samples (DXVK's path for unusual resolve modes)
#   3  vkCmdResolveImage
# The engine resolves the scene twice a frame into the same texture, before and after the effects. On the Adreno 830
# (driver 0.800) with 0 the texture kept the picture from before the effects, and the bloom pass that draws it back
# over the scene removed every effect (muzzle flash, explosions) with r_aaSamples 4; with 1 it was black.
C=$D/src/src/dxvk/dxvk_context.cpp
grep -q BO1_RESOLVE $C || python3 - "$C" <<'EOF' || exit 1
import sys
p = sys.argv[1]
s = open(p).read()
old = """    bool useHw = !ensureImageCompatibility(dstImage, dstUsage)
              || !ensureImageCompatibility(srcImage, srcUsage);

    // If possible, fold resolve into active render pass
    if (!useHw && !useFb && resolveImageInline(dstImage, srcImage, region, format, mode, stencilMode))
      return;
"""
new = """    // BO1_RESOLVE: see tools/native/build-dxvk.sh
    static int bo1Resolve = -1;
    if (bo1Resolve < 0) {
      const char* env = std::getenv("BO1_DXVK_RESOLVE");
      bo1Resolve = env ? std::atoi(env) : 0;
    }
    bool bo1Color = !(formatInfo->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    if (bo1Color && bo1Resolve == 2) {
      useFb = true;
      srcUsage.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      srcUsage.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      srcUsage.access = VK_ACCESS_SHADER_READ_BIT;
    }

    bool useHw = !ensureImageCompatibility(dstImage, dstUsage)
              || !ensureImageCompatibility(srcImage, srcUsage);
    if (bo1Color && bo1Resolve == 3)
      useHw = true;

    // If possible, fold resolve into active render pass
    if (!useHw && !useFb && !(bo1Color && bo1Resolve == 1)
     && resolveImageInline(dstImage, srcImage, region, format, mode, stencilMode))
      return;
"""
assert s.count(old) == 1
s = s.replace(old, new)
if "#include <cstdlib>" not in s:
    s = "#include <cstdlib>\n" + s
open(p, "w").write(s)
EOF

if [ "$TARGET" = linux ]; then
  B=$D/build-linux
  [ -f $B/build.ninja ] || CC=clang-21 CXX=clang++-21 meson setup $B $D/src --buildtype release \
    -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Ddxvk_native_wsi=sdl2 > $D/setup-linux.log 2>&1 \
    || { tail -20 $D/setup-linux.log; exit 1; }
else
  if [ "$TARGET" = x86_64 ]; then
    TRIPLE=x86_64-linux-android$API; FAMILY=x86_64; SDLDIR=$A/sdl-x86_64
  else
    TARGET=arm64-v8a; TRIPLE=aarch64-linux-android$API; FAMILY=aarch64; SDLDIR=$A/sdl
  fi
  [ -f $SDLDIR/lib/libSDL2.so ] || { echo "no $SDLDIR/lib/libSDL2.so: run build-android.sh first"; exit 1; }
  B=$D/build-android-$TARGET
  mkdir -p $D/cross/$TARGET
  # SDL2 for pkg-config (DXVK only needs its headers: it loads the library by name at run time)
  cat > $D/cross/$TARGET/sdl2.pc <<EOF
Name: sdl2
Description: SDL2 for Android ($TARGET)
Version: 2.30.0
Cflags: -I$SDLDIR/include/SDL2 -I$SDLDIR/include
Libs: -L$SDLDIR/lib -lSDL2
EOF
  cat > $D/cross/$TARGET/cross.txt <<EOF
[binaries]
c = '$TC/bin/$TRIPLE-clang'
cpp = '$TC/bin/$TRIPLE-clang++'
ar = '$TC/bin/llvm-ar'
strip = '$TC/bin/llvm-strip'
pkg-config = 'pkg-config'

[built-in options]
pkg_config_path = '$D/cross/$TARGET'
c_args = ['-fPIC']
cpp_args = ['-fPIC']
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-static-libstdc++', '-Wl,-z,max-page-size=16384']

[properties]
pkg_config_libdir = '$D/cross/$TARGET'

[host_machine]
system = 'android'
cpu_family = '$FAMILY'
cpu = '$FAMILY'
endian = 'little'
EOF
  [ -f $B/build.ninja ] || meson setup $B $D/src --cross-file $D/cross/$TARGET/cross.txt --buildtype release \
    -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Ddxvk_native_wsi=sdl2 > $B.setup.log 2>&1 \
    || { tail -30 $B.setup.log; exit 1; }
fi
nice -n 19 ionice -c3 ninja -C $B -j ${JOBS:-6} -k 0 src/d3d9/libdxvk_d3d9.so > $B.build.log 2>&1
echo "ninja exit=$?  errors: $(grep -c "error:" $B.build.log)"
grep -E "error:" -A3 $B.build.log | head -${2:-40}
ls -la $B/src/d3d9/libdxvk_d3d9.so* 2>/dev/null
