#!/bin/bash
# run-arm64.sh <seconds> [+commands...]: run the AArch64 engine under qemu-user against /bo1/game
SECS="${1:-30}"; shift
cd /bo1/game
ulimit -c 0
R=/bo1/arm64root
timeout --signal=KILL "$SECS" nice -n 10 qemu-aarch64-static -L $R -E LD_LIBRARY_PATH=$R/usr/lib/llvm-21/lib:$R/usr/lib/aarch64-linux-gnu \
  /bo1/p32tree/build/arm64/bin/bo1 +set fs_b /bo1/game +set fs_h /bo1/home "$@"
echo "[run] exit=$?"
