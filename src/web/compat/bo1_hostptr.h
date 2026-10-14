// bo1_hostptr.h - pointer types that stay native (64-bit) in the 32-bit-pointer build (tools/p32).
//
// In that build every pointer object the engine declares is 4 bytes. The host's functions (C library, OpenGL, jpeg)
// still write and read native pointers through their out-parameters: `strtol(s, &end, 10)`, `glShaderSource(.., &src,
// ..)`. Declare such a variable with one of these names. The rewriter does not touch this file (p32tree.py EXCLUDE),
// so the stars below are native; in every other build they are ordinary pointers.
#pragma once

typedef char *bo1_host_str;
typedef const char *bo1_host_cstr;
typedef void *bo1_host_voidp;
typedef unsigned char *bo1_host_bytes;

#ifdef __cplusplus
template <class T> using bo1_host_ptr = T *;
#endif
