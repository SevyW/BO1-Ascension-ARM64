// xmmintrin.h - the SSE header the engine includes, per host:
//   Emscripten: its <xmmintrin.h> (wasm SIMD);
//   native x86: the compiler's;
//   native AArch64 (Android, Linux): sse2neon (third_party/sse2neon), which implements SSE..SSE4.2 on NEON.
// Followed by the MMX intrinsics MSVC's <xmmintrin.h> brings in (mmintrin.h in this folder).
#pragma once
#if defined(__aarch64__)
// __int64 is the engine's (q_shared.h: long long); sse2neon would make it int64_t, which `unsigned __int64` cannot
// take. Bink's radbase.h defines u8..f64 as macros and sse2neon has parameters of those names.
#ifndef __int64
#define __int64 long long
#endif
#pragma push_macro("u8")
#undef u8
#pragma push_macro("s8")
#undef s8
#pragma push_macro("u16")
#undef u16
#pragma push_macro("s16")
#undef s16
#pragma push_macro("u32")
#undef u32
#pragma push_macro("s32")
#undef s32
#pragma push_macro("u64")
#undef u64
#pragma push_macro("s64")
#undef s64
#pragma push_macro("f32")
#undef f32
#pragma push_macro("f64")
#undef f64
#include <sse2neon/sse2neon.h>
#pragma pop_macro("u8")
#pragma pop_macro("s8")
#pragma pop_macro("u16")
#pragma pop_macro("s16")
#pragma pop_macro("u32")
#pragma pop_macro("s32")
#pragma pop_macro("u64")
#pragma pop_macro("s64")
#pragma pop_macro("f32")
#pragma pop_macro("f64")
#else
#include_next <xmmintrin.h>
#endif
#include "mmintrin.h"
