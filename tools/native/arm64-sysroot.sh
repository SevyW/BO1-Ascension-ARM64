#!/bin/bash
# Builds /bo1/arm64root: an AArch64 Debian sysroot (headers + libraries) extracted from packages, for cross-compiling
# with clang --target=aarch64-linux-gnu --sysroot and running under qemu-aarch64-static -L. Nothing is installed for
# arm64 in the container itself (the packages are only unpacked).
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update -qq > /dev/null 2>&1
apt-get install -y -qq --no-install-recommends qemu-user-static > /dev/null 2>&1
mkdir -p /bo1/arm64debs/partial /bo1/arm64root
apt-get install -y --download-only --no-install-recommends -o Dir::Cache::archives=/bo1/arm64debs \
  libc6-dev:arm64 linux-libc-dev:arm64 libgcc-14-dev:arm64 libstdc++-14-dev:arm64 libc++-21-dev:arm64 libc++abi-21-dev:arm64 libunwind-21-dev:arm64 \
  libsdl2-dev:arm64 libgles-dev:arm64 libegl-dev:arm64 libgl1-mesa-dri:arm64 libegl1:arm64 libgles2:arm64 libglx-mesa0:arm64 libegl-mesa0:arm64 \
  > /bo1/arm64debs/download.log 2>&1
echo "debs: $(ls /bo1/arm64debs/*.deb 2>/dev/null | wc -l)"
for d in /bo1/arm64debs/*_arm64.deb /bo1/arm64debs/*_all.deb; do [ -f "$d" ] && dpkg -x "$d" /bo1/arm64root; done
ls /bo1/arm64root/usr/lib/aarch64-linux-gnu | wc -l
ls /bo1/arm64root/usr/lib/aarch64-linux-gnu/libSDL2.so /bo1/arm64root/usr/lib/aarch64-linux-gnu/libGLESv2.so /bo1/arm64root/usr/lib/llvm-21/lib/libc++.so 2>&1 | head -4
which qemu-aarch64-static
# usrmerge links (the packages are unpacked, not installed), the kernel headers (linux-libc-dev is Architecture: all
# and already in the container) and a way back to /bo1: qemu -L shows this directory as the root listing.
R=/bo1/arm64root
for d in lib bin sbin; do [ -e $R/$d ] || ln -s usr/$d $R/$d; done
cp -a /usr/include/linux /usr/include/asm-generic $R/usr/include/
rm -rf $R/usr/include/aarch64-linux-gnu/asm; cp -rL /usr/lib/linux/uapi/arm64/asm $R/usr/include/aarch64-linux-gnu/asm
ln -sfn /bo1 $R/bo1
