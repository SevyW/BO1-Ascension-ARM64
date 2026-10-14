#!/bin/bash
# gfx-arm64.sh <seconds> <shot-prefix> [+commands...]: the AArch64 engine under qemu-user with a window on a virtual
# X display (the sysroot's Mesa, software rendering, emulated: slow). Screenshots every 30 s to /bo1/home/shots.
SECS="${1:-120}"; PFX="${2:-a64}"; shift 2
mkdir -p /bo1/home/shots
R=/bo1/arm64root
export DISPLAY=:99
pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
cd /bo1/game
ulimit -c 0
# (the environment reaches the emulated program as it is; qemu's -E splits values at commas)
env LD_LIBRARY_PATH=$R/usr/lib/llvm-21/lib:$R/usr/lib/aarch64-linux-gnu LP_NUM_THREADS=${LP_NUM_THREADS:-8} LIBGL_ALWAYS_SOFTWARE=1 D3D9SHIM_LOG=${D3D9SHIM_LOG:-2} BO1_TESTPAD="$BO1_TESTPAD" nice -n 10 qemu-aarch64-static -L $R /bo1/p32tree/build/arm64/bin/bo1 +set fs_b /bo1/game +set fs_h /bo1/home +set r_fullscreen 0 "$@" > /bo1/home/gfx_$PFX.log 2>&1 &
PID=$!
n=0; t=0
while [ $t -lt $SECS ] && kill -0 $PID 2>/dev/null; do
  sleep 30; t=$((t+30)); n=$((n+1))
  import -window root /bo1/home/shots/${PFX}_$(printf %02d $n).png 2>/dev/null
done
if kill -0 $PID 2>/dev/null; then kill -9 $PID; echo "[gfx] stopped after ${t}s"; else wait $PID; echo "[gfx] engine exited by itself, code $?"; fi
