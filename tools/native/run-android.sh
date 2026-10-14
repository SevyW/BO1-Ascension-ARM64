#!/bin/bash
# run-android.sh <seconds> [+commands...]: the Android engine library under qemu-user with the bionic of an Android
# system image (/system and /apex in the container point into /bo1/android/aroot), through the loader stub's test
# executable. No Java, no display: for the dedicated server.
SECS="${1:-30}"; shift
cd /bo1/game
ulimit -c 0
B=/bo1/p32tree/build/android/bin
LD_LIBRARY_PATH=/bo1/android/stubs:$B timeout --signal=KILL "$SECS" nice -n 10 qemu-aarch64-static $B/bo1_boot_test $B/libbo1.so +set fs_b /bo1/game +set fs_h /bo1/home "$@"
echo "[run] exit=$?"
