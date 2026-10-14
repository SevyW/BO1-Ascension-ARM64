// native_posix.h - libc calls whose structs do not survive the x86 build's -malign-double (cmake/native.cmake).
//
// The engine's struct layouts are MSVC's, where double and __int64 members align to 8; the 32-bit x86 native build
// gets that with -malign-double. glibc's i386 struct stat holds 64-bit members at 4-byte offsets, so a TU compiled
// with the flag reads it wrong. native_posix.c is compiled without the flag and returns the fields in bo1_stat, whose
// layout is the same either way. On other architectures this is just an indirection.
//
// Included by src/web/compat/bo1_win_internal.h after <sys/stat.h>: stat / fstat then name these.
#pragma once
#include <stdint.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bo1_stat_time
{
    int64_t tv_sec;
    int64_t tv_nsec;
};

// members are named like struct stat's so the callers do not change (glibc: st_mtime is st_mtim.tv_sec)
struct bo1_stat
{
    int64_t st_size;
    struct bo1_stat_time st_atim;
    struct bo1_stat_time st_mtim;
    struct bo1_stat_time st_ctim;
    uint32_t st_mode;
    uint32_t reserved;
};

int bo1_stat(const char *path, struct bo1_stat *out);
int bo1_fstat(int fd, struct bo1_stat *out);

#ifdef __cplusplus
}
#endif

#ifndef BO1_NATIVE_POSIX_IMPL
#undef stat
#undef fstat
#define stat bo1_stat
#define fstat bo1_fstat
#endif
