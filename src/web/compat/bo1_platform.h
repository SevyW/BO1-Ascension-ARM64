// bo1_platform.h - the few host services the Win32 emulation and the platform layer need beyond POSIX, for both
// hosts of this code: Emscripten (the browser / Node build, cmake/web.cmake) and a native POSIX system (BO1_NATIVE,
// cmake/native.cmake: Linux, Android). Not for engine code.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bo1_hostptr.h"

#ifndef BO1_NATIVE
// ===================================================================================================================
// Emscripten
// ===================================================================================================================
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/heap.h>
#include <pthread.h>

static inline double bo1_plat_now_ms(void) { return emscripten_get_now(); }
static inline int bo1_plat_core_count(void) { return emscripten_num_logical_cores(); }
static inline void bo1_plat_set_thread_name(const char *name) { emscripten_set_thread_name(pthread_self(), name); }
// the address space the engine lives in (the wasm memory) and its fixed maximum
static inline uint64_t bo1_plat_address_space(void) { return (uint64_t)emscripten_get_heap_size(); }
static inline uint64_t bo1_plat_memory_total(void) { return (uint64_t)emscripten_get_heap_max(); }
// wait while *addr == expected, at most msec; wake every waiter
static inline void bo1_plat_futex_wait(volatile void *addr, uint32_t expected, double msec)
{
    emscripten_futex_wait(addr, expected, msec);
}
static inline void bo1_plat_futex_wake_all(volatile void *addr) { emscripten_futex_wake(addr, 0x7FFFFFFF); }
static inline void bo1_plat_log_stack(const char *msg) { emscripten_log(EM_LOG_ERROR | EM_LOG_C_STACK, "%s", msg); }

#else
// ===================================================================================================================
// native POSIX (Linux, Android)
// ===================================================================================================================
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#endif

#define EMSCRIPTEN_KEEPALIVE __attribute__((used))

static inline double bo1_plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
static inline int bo1_plat_core_count(void)
{
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
static inline void bo1_plat_set_thread_name(const char *name)
{
    char shortName[16];   // Linux: 15 characters + NUL, longer names fail
    snprintf(shortName, sizeof(shortName), "%s", name ? name : "");
    pthread_setname_np(pthread_self(), shortName);
}
// native: the engine's pointers are 32-bit values (a 32-bit process, or a 64-bit one that keeps engine memory below
// 4 GB), so the address space it can see ends there
static inline uint64_t bo1_plat_address_space(void) { return 0x100000000ull; }
// what the engine may plan with: physical memory, capped at 2 GB like a 32-bit Windows process
static inline uint64_t bo1_plat_memory_total(void)
{
    const uint64_t cap = 2048ull * 1024 * 1024;
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    const uint64_t phys = pages > 0 && pageSize > 0 ? (uint64_t)pages * (uint64_t)pageSize : cap;
    return phys < cap ? phys : cap;
}
static inline void bo1_plat_futex_wait(volatile void *addr, uint32_t expected, double msec)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(msec / 1000.0);
    ts.tv_nsec = (long)((msec - (double)ts.tv_sec * 1000.0) * 1e6);
#if defined(SYS_futex_time64) && __SIZEOF_POINTER__ == 4
    // 32-bit ABIs: time_t may be 64-bit (_TIME_BITS=64) while SYS_futex reads a 32-bit timespec; futex_time64
    // always reads two 64-bit fields
    struct { int64_t tv_sec; int64_t tv_nsec; } ts64 = { (int64_t)ts.tv_sec, (int64_t)ts.tv_nsec };
    syscall(SYS_futex_time64, addr, FUTEX_WAIT, expected, &ts64, (void *)0, 0);
#else
    syscall(SYS_futex, addr, FUTEX_WAIT, expected, &ts, (void *)0, 0);
#endif
}
static inline void bo1_plat_futex_wake_all(volatile void *addr)
{
#if defined(SYS_futex)
    syscall(SYS_futex, addr, FUTEX_WAKE, 0x7FFFFFFF, (void *)0, (void *)0, 0);
#else
    syscall(SYS_futex_time64, addr, FUTEX_WAKE, 0x7FFFFFFF, (void *)0, (void *)0, 0);
#endif
}
static inline void bo1_plat_log_stack(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
#if !defined(__ANDROID__)
    bo1_host_voidp frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
#endif
}
#endif
