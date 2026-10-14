# sse2neon (vendored)

* Upstream: https://github.com/DLTcollab/sse2neon, tag `v1.8.0`; fetched 2026-10-09.
* License: MIT (see `LICENSE`). Unmodified.
* Used by: `src/web/compat/xmmintrin.h` on AArch64, where the engine's SSE intrinsics (about 140 uses, 67 distinct,
  mostly in `src/gfx_d3d`) are compiled as NEON.
