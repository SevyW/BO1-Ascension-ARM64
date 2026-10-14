// emmintrin.h - SSE2. AArch64: sse2neon covers it (xmmintrin.h in this folder); elsewhere the host's header.
#pragma once
#if defined(__aarch64__)
#include "xmmintrin.h"
#else
#include_next <emmintrin.h>
#endif
