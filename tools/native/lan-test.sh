#!/bin/bash
# lan-test.sh [seconds]: hosting and finding a game from the menus, on one machine (inside the bo1-x64 container).
# A host with a window on a second virtual display starts Zombies > HOST from the Play screen (scripted pad); a
# second game opens the join screen, which must list the host ("LAN <address> <map> <players>"), picks it and
# joins. Logs: /bo1/home/lan_host.log, /bo1/home/gfx_lan.log; screenshots of the joining game
# /bo1/home/shots/lan_NN.png.
SECS="${1:-200}"
BIN=/bo1/p32tree/build/x64/bin/bo1
mkdir -p /bo1/home-server /bo1/home/shots
pgrep -f "Xvfb :98" >/dev/null || { Xvfb :98 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
cd /bo1/game; ulimit -c 0
# host: title (A), the Play screen, MODE two rows up, one step right of ZOMBIES: SOLO = HOST, Start
DISPLAY=:98 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=4 BO1_TESTPAD="25:a:400,34:cmd@play,38:up:300,40:up:300,42:right:300,45:start:300" \
  nice -n 10 $BIN +set fs_b /bo1/game +set fs_h /bo1/home-server +set r_fullscreen 0 +set bo1_zombies 1 +set net_port 28960 \
  > /bo1/home/lan_host.log 2>&1 &
HOST=$!
sleep 80
# the joining game: title (A), the join screen, five rows down to the first listed game, A puts its address on the
# line, Start joins
BO1_TESTPAD="25:a:400,50:cmd@joinmenu,58:down:200,59:down:200,60:down:200,61:down:200,62:down:200,64:a:300,70:start:300" \
  /bo1/gfx-x64.sh "$SECS" lan +set bo1_zombies 1 +set net_port 28961 +set name Guest +set bo1_slot 1
kill -9 $HOST 2>/dev/null
echo "--- host"
grep -a -n -i -E "play:|Opening IP socket|^IP:|coop:|Guest|fatal|signal 11|dropped|timed out" /bo1/home/lan_host.log | grep -a -v -i "redundant" | head -20 | cut -c1-200
echo "--- joining game"
grep -a -n -i -E "Opening IP socket|coop: joining|Guest|fatal|signal 11|ERROR:|timed out" /bo1/home/gfx_lan.log | grep -a -v -i "redundant\|could not load\|Expression" | head -20 | cut -c1-200
