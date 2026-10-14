#!/bin/bash
# fx-arm64.sh <prefix>: do the weapon effects draw in the AArch64 build? Like fx-test.sh, with the AArch64 Linux
# engine under qemu-user on its own virtual display (:97): slow, so the times are long. Starts zombie_theater, fires
# every 6 s from FXA_FROM (default 600 s: the map takes that long to load under qemu) and throws grenades 50 s and
# 110 s later, saves a frame every 1.5 s for 3 minutes
# (/bo1/home/shots/<prefix>_NNN.png) and contact sheets of 40 frames each (<prefix>_sheetN.png).
PFX="${1:-fxa}"
R=/bo1/arm64root
mkdir -p /bo1/home/shots
export DISPLAY=:97
pgrep -f "Xvfb :97" >/dev/null || { Xvfb :97 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
PAD=""
FROM=${FXA_FROM:-600}
for t in $(seq $FROM 6 $((FROM + 174))); do PAD="$PAD$t:rt:500,"; done
PAD="${PAD}$((FROM + 50)):rb:800,$((FROM + 110)):rb:800"
cd /bo1/game; ulimit -c 0
env LD_LIBRARY_PATH=$R/usr/lib/llvm-21/lib:$R/usr/lib/aarch64-linux-gnu LP_NUM_THREADS=8 LIBGL_ALWAYS_SOFTWARE=1 BO1_TESTPAD="$PAD" \
  nice -n 10 qemu-aarch64-static -L $R /bo1/p32tree/build/arm64/bin/bo1 +set fs_b /bo1/game +set fs_h /bo1/home +set r_fullscreen 0 \
  +set bo1_zombies 1 +devmap zombie_theater > /bo1/home/gfx_$PFX.log 2>&1 &
PID=$!
sleep $FROM
for n in $(seq 1 120); do
  import -window root /bo1/home/shots/${PFX}_$(printf %03d $n).png 2>/dev/null
  sleep 1.2
done
kill -9 $PID 2>/dev/null
cd /bo1/home/shots
for sheet in 0 1 2; do
  rows=""
  for r in 0 1 2 3; do
    files=""
    for c in 1 2 3 4 5 6 7 8 9 10; do files="$files ${PFX}_$(printf %03d $((sheet * 40 + r * 10 + c))).png"; done
    convert $files -resize 256x144 +append ${PFX}_row$r.png
    rows="$rows ${PFX}_row$r.png"
  done
  convert $rows -append ${PFX}_sheet$sheet.png && rm -f $rows
done
ls ${PFX}_sheet*.png
