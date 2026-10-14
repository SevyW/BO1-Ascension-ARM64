#!/bin/bash
# coop-test.sh <seconds> [map]: zombies co-op over real UDP on one machine (inside the bo1-x64 container): a
# dedicated server on port 28960 and one rendering client that joins it by address. Logs: /bo1/home/coop_server.log
# and /bo1/home2/../gfx_coop.log; screenshots /bo1/home/shots/coop_NN.png.
SECS="${1:-150}"; MAP="${2:-zombie_theater}"
BIN=/bo1/p32tree/build/x64/bin/bo1
SHARED="+set bo1_zombies 1 +set logfile 2 +set systemlink 1"
mkdir -p /bo1/home-server /bo1/home/shots
cd /bo1/game; ulimit -c 0
nice -n 10 $BIN +set fs_b /bo1/game +set fs_h /bo1/home-server +set dedicated 1 $SHARED +set net_port 28960 \
  +set sv_maxclients 4 +set bo1_expected_players 1 +set bo1_lobby_chars "0 1 2 3" +set sv_maxRate 25000 +map $MAP \
  > /bo1/home/coop_server.log 2>&1 &
SERVER=$!
# the server is up when it says it spawned the map
for i in $(seq 1 90); do grep -a -q "Hitch warning\|Huffman Took" /bo1/home/coop_server.log && break; sleep 1; done
/bo1/gfx-x64.sh "$SECS" coop $SHARED +set net_port 28961 +set name Guest +set bo1_slot 0 +connect 127.0.0.1:28960
kill -9 $SERVER 2>/dev/null
echo "--- server"
grep -a -n -i -E "Opening IP socket|^IP:|coop:|connect|Guest|ASSERT|fatal|dropped|timed out" /bo1/home/coop_server.log | grep -a -v -i "redundant" | head -25 | cut -c1-200
echo "--- client"
grep -a -n -i -E "Opening IP socket|connect|Guest|^ASSERT|fatal|ERROR:|timed out|BO1_MUSIC" /bo1/home/gfx_coop.log | grep -a -v -i "redundant\|could not load\|Expression" | head -25 | cut -c1-200
