# dlmalloc (vendored)

* Upstream: https://gee.cs.oswego.edu/pub/misc/malloc-2.8.6.c (Doug Lea's malloc, version 2.8.6, 2012-08-29);
  fetched 2026-10-09.
* License: public domain (CC0), see the head of `malloc.c`. Unmodified.
* Used by: `src/native/native_lowmem.c`, which includes it as an mspace-only allocator over one region below 4 GB
  (the heap of the 64-bit native build, whose pointer objects are 32 bits; see `tools/p32`).
