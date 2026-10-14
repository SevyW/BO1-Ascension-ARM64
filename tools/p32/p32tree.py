#!/usr/bin/env python3
"""p32tree.py - builds the 32-bit-pointer copy of the source tree for 64-bit native builds.

  tools/p32/p32tree.py --db <build dir with compile_commands.json> --out <dir> [--jobs N] [--tool <p32rewrite>]

1. Runs p32rewrite (tools/p32/p32rewrite.cpp) over every translation unit of the compilation database, in parallel.
2. Copies the repository (without build/ and .git/) to <out>.
3. Applies the tool's edits: " BO1_P32 " after pointer stars (`__ptr32 __uptr` in 64-bit builds, see
   cmake/native.cmake), `long` -> `int `, and BO1_NAT(...) around the callee of indirect calls.

Line numbers do not change (insertions only, within a line). Files under the directories in EXCLUDE are copied
unchanged: they talk to the host (SDL, the C library) with native pointers.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# src/native talks to the host with native pointers. The C libraries are self-contained (and C cannot
# initialize a static 32-bit pointer from an address, which their tables do).
EXCLUDE = ["src/native", "tools", "build", "web", "docs", "mods", "data", "images", "src/web/d3d9/test", "src/web/test",
           "src/web/tools", "third_party", "src/libs", "src/zlib", "src/jpeg", "src/speex", "src/steam", "src/web/compat/steam", "src/minilzo",
           "src/web/compat/bo1_hostptr.h"]
# ... but zlib's `long` fields (uLong) sit inside engine structs with MSVC sizes (qcommon/unzip.h): 32 bits
LONG_ONLY = ["src/zlib"]
# Hand-written platform code (Win32 emulation, D3D9 shim, entry point): its local pointer variables stay native.
# It hands them to the C library and to OpenGL, and its fields and globals (the layouts the engine sees) are
# still annotated.
LOCALS_NATIVE = ["src/web"]
COPY_SKIP = {"build", ".git", "web", "images", ".railway"}
MARK = " BO1_P32 "


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--tool", default=None)
    ap.add_argument("--resource-dir", default="/usr/lib/llvm-21/lib/clang/21")
    ap.add_argument("--batch", type=int, default=12)
    args = ap.parse_args()

    root = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", ".."))
    tool = args.tool or os.path.join(args.db, "p32rewrite")
    db = json.load(open(os.path.join(args.db, "compile_commands.json")))
    sources = sorted({os.path.realpath(e["file"]) for e in db})
    sources = [s for s in sources if s.startswith(root + os.sep)]
    batches = [sources[i:i + args.batch] for i in range(0, len(sources), args.batch)]
    print(f"p32tree: {len(sources)} translation units, {len(batches)} batches, {args.jobs} jobs", flush=True)

    edits = {}  # path -> set of (kind, begin, end)
    warnings = set()  # (path, line): dependent pointer-to-pointer left native (clang 21 crash)

    def run(batch):
        cmd = [tool, "-p", args.db, "--root", root]
        for e in EXCLUDE:
            cmd += ["--exclude", e]
        for e in LONG_ONLY:
            cmd += ["--long-only", e]
        for e in LOCALS_NATIVE:
            cmd += ["--locals-native", e]
        cmd += [f"--extra-arg=-resource-dir={args.resource_dir}"] + batch
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace")
        return p.stdout

    done = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for out in pool.map(run, batches):
            for line in out.splitlines():
                parts = line.split("\t")
                if len(parts) == 3 and parts[0] in ("P", "L", "F"):
                    edits.setdefault(parts[1], set()).add((parts[0], int(parts[2]), 0))
                elif len(parts) == 3 and parts[0] == "W":
                    warnings.add((parts[1], int(parts[2])))
                elif len(parts) == 4 and parts[0] in ("N", "A", "D", "U", "I", "V", "B"):
                    edits.setdefault(parts[1], set()).add((parts[0], int(parts[2]), int(parts[3])))
            done += 1
            if done % 20 == 0 or done == len(batches):
                print(f"p32tree: {done}/{len(batches)} batches, {sum(len(v) for v in edits.values())} edits in "
                      f"{len(edits)} files", flush=True)

    # copy the tree
    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    for name in os.listdir(root):
        if name in COPY_SKIP:
            continue
        src = os.path.join(root, name)
        dst = os.path.join(args.out, name)
        if os.path.isdir(src):
            shutil.copytree(src, dst, symlinks=True)
        else:
            shutil.copy2(src, dst)

    # apply: every edit becomes (offset, order, bytes to delete, text to insert); insertions never overlap deletions
    counts = {"P": 0, "L": 0, "F": 0, "N": 0, "A": 0, "D": 0, "U": 0, "I": 0, "V": 0, "B": 0}
    for path, items in edits.items():
        rel = os.path.relpath(path, root)
        target = os.path.join(args.out, rel)
        if not os.path.isfile(target):
            continue
        data = open(path, "rb").read()
        # (offset, order at that offset, tie-break, bytes to delete, text). Wrappers nest: of two that open at the same
        # offset the longer one opens first; of two that close at the same offset the shorter one closes first.
        ops = []
        for kind, begin, end in items:
            if kind == "P":
                if data[begin:begin + 1] == b"*":
                    ops.append((begin + 1, 1, 0, 0, MARK.encode()))
                    counts["P"] += 1
            elif kind == "L":
                if data[begin:begin + 4] == b"long":
                    ops.append((begin, 1, 0, 4, b"int "))
                    counts["L"] += 1
            elif kind == "F":
                if data[begin:begin + 4] == b"long":
                    ops.append((begin, 1, 0, 4, b"    "))
                    counts["F"] += 1
            elif kind in ("N", "A", "D", "U", "I", "V", "B"):
                if 0 <= begin < end <= len(data):
                    opener = {"N": b"BO1_NAT(", "A": b"BO1_ADDR(", "D": b"BO1_NATD(", "U": b"BO1_U32(", "I": b"BO1_IDX(", "V": b"BO1_VARG(", "B": b"BO1_NATP("}[kind]
                    ops.append((begin, 2, -end, 0, opener))
                    ops.append((end, 0, -begin, 0, b")"))
                    counts[kind] += 1
        ops.sort(key=lambda o: (o[0], o[1], o[2]))
        out = bytearray()
        last = 0
        for off, _, _, delete, text in ops:
            if off < last:
                continue
            out += data[last:off]
            out += text
            last = off + delete
        out += data[last:]
        open(target, "wb").write(bytes(out))
    for path, line in sorted(warnings):
        print(f"p32tree: note: {os.path.relpath(path, root)}:{line}: template pointer-to-pointer left native")
    print(f"p32tree: {counts['P']} pointers, {counts['L']} longs, {counts['F']} long doubles, {counts['N']} native-pointer wraps, {counts['A']} static addresses, {counts['U']} int-to-pointer casts, {counts['I']} unsigned offsets, {counts['V']} variadic pointers, {counts['B']} downcasts in {len(edits)} "
          f"files -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
