#!/bin/bash
# linux-clang.sh - the compiler for tools/syntax-check.sh on Linux: clang++ with the forwarding headers of
# linux-case.py first (CASE_DIR, default /tmp/case). Use as CLANGXX=tools/syntax-check/linux-clang.sh.
exec "${REAL_CLANGXX:-clang++}" -isystem "${CASE_DIR:-/tmp/case}" "$@"
