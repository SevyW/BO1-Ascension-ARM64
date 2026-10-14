#!/bin/bash
# fx-test.sh <prefix> [gles|vulkan]: do the weapon effects draw, and do the weapon sounds play? Starts
# zombie_theater with a window on the virtual display (inside the bo1-x64 container), fires from 44 s and throws a
# grenade at 48 s (before the first zombies arrive), and saves a screenshot every 0.4 s from 44 s to 60 s: /bo1/home/shots/<prefix>_NN.png (the
# explosion is about 4 s after the throw) and a contact sheet <prefix>_sheet.png. The sound goes to a file instead
# of a device (SDL's disk driver); its level per second is printed: the shots are from 44 s.
PFX="${1:-fx}"; REN="${2:-gles}"
mkdir -p /bo1/home/shots
export DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=4 XDG_RUNTIME_DIR=/tmp BO1_RENDERER=$REN
export BO1_TESTPAD="${FX_PAD:-44:rt:3500,48:rb:700}"
export SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/bo1/home/fx_audio.raw
rm -f $SDL_DISKAUDIOFILE
pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
cd /bo1/game; ulimit -c 0
nice -n 10 /bo1/p32tree/build/x64/bin/bo1 +set fs_b /bo1/game +set fs_h /bo1/home +set r_fullscreen 0 +set bo1_zombies 1 \
  +devmap zombie_theater > /bo1/home/gfx_$PFX.log 2>&1 &
PID=$!
sleep ${FX_FROM:-44}
for n in $(seq 1 40); do
  import -window root /bo1/home/shots/${PFX}_$(printf %02d $n).png 2>/dev/null
  sleep 0.25
done
kill -9 $PID 2>/dev/null
cd /bo1/home/shots
rows=""
for r in 0 1 2 3; do
  files=""
  for c in 1 2 3 4 5 6 7 8 9 10; do files="$files ${PFX}_$(printf %02d $((r * 10 + c))).png"; done
  convert $files -resize 256x144 +append ${PFX}_row$r.png
  rows="$rows ${PFX}_row$r.png"
done
convert $rows -append ${PFX}_sheet.png && rm -f $rows
grep -a "native: sound" /bo1/home/gfx_$PFX.log
python3 - <<'EOF'
import array, os
p = "/bo1/home/fx_audio.raw"
if not os.path.exists(p):
    print("no audio file")
else:
    a = array.array("h")
    with open(p, "rb") as f:
        data = f.read()
    a.frombytes(data[:len(data) // 2 * 2])
    per = 48000 * 2   # samples per second, stereo (the mixer's format; see the log line above)
    out = []
    for s in range(0, len(a), per):
        chunk = a[s:s + per]
        if chunk:
            out.append(int((sum(x * x for x in chunk[::16]) / max(1, len(chunk[::16]))) ** 0.5))
    print("audio seconds:", len(out))
    print("level per second (second:level, silent seconds left out):", " ".join("%d:%d" % (i, v) for i, v in enumerate(out) if v > 0))
EOF
