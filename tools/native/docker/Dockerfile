# The build environment of the native builds (Linux x86-64 and Android arm64): Debian 13 with LLVM 21 from
# apt.llvm.org, the tools DXVK, FFmpeg and the APK need. The repository goes to /bo1/src, the work folder is /bo1:
#   docker build -t bo1-build tools/native/docker
#   docker run -d --name bo1-x64 -v <work folder>:/bo1 bo1-build
#   git clone <this repository> <work folder>/src
#   docker exec bo1-x64 /bo1/src/tools/native/build-all-android.sh
FROM debian:13
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-21 main" \
       > /etc/apt/sources.list.d/llvm21.list \
    && apt-get update && apt-get install -y --no-install-recommends \
      git cmake ninja-build make meson pkg-config python3 file less procps util-linux rsync wget unzip zip xz-utils \
      p7zip-full e2fsprogs erofs-utils glslang-tools \
      clang-21 lld-21 llvm-21-dev libclang-21-dev libclang-cpp21-dev libc++-21-dev libc++abi-21-dev libunwind-21-dev \
      clang lld llvm libc6-dev \
      libsdl2-dev libgles-dev libegl-dev libgl1-mesa-dri libegl1 libgles2 libvulkan1 mesa-vulkan-drivers \
      openjdk-21-jdk-headless \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /bo1/src
CMD ["sleep", "infinity"]
