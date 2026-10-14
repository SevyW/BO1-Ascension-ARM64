#!/usr/bin/env python3
"""linux-case.py <mingw include dir> <out dir> - for tools/syntax-check.sh on a case-sensitive file system.

The engine spells Windows headers the way MSVC accepts (<Windows.h>, <XAudio2.h>, <MMSystem.h>); MinGW's files are
lower case. macOS does not care; Linux does. This writes one forwarding header per spelling the sources use that
differs from MinGW's (or from tools/syntax-check/include) only by case, into <out dir>. Put that directory first:

  python3 tools/syntax-check/linux-case.py /usr/i686-w64-mingw32/include /tmp/case
  CLANGXX=<a wrapper that adds -isystem /tmp/case> MINGW_ROOT=<...> tools/syntax-check.sh <files>
"""
import os
import re
import sys

mingw, out = sys.argv[1], sys.argv[2]
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
have = {}
for base in (mingw, os.path.join(root, "tools", "syntax-check", "include")):
    for dirpath, _, files in os.walk(base, followlinks=True):
        if os.sep + "c++" in dirpath:
            continue
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), base).replace(os.sep, "/")
            have.setdefault(rel.lower(), rel)
used = set()
pattern = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
for top in ("src", "tl", "DemonWare"):
    for dirpath, _, files in os.walk(os.path.join(root, top)):
        for f in files:
            if f.endswith((".h", ".hpp", ".c", ".cpp", ".inl")):
                try:
                    text = open(os.path.join(dirpath, f), encoding="latin-1").read()
                except OSError:
                    continue
                used.update(m.replace("\\", "/") for m in pattern.findall(text))
count = 0
for name in sorted(used):
    real = have.get(name.lower())
    if not real or real == name:
        continue
    path = os.path.join(out, name)
    os.makedirs(os.path.dirname(path) or out, exist_ok=True)
    with open(path, "w") as f:
        f.write("#include <%s>\n" % real)
    count += 1
print("%d forwarding headers in %s" % (count, out))
