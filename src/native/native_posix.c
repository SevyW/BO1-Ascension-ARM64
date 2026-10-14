// native_posix.c - see native_posix.h. Compiled WITHOUT -malign-double (cmake/native.cmake, BO1_NATIVE_PLAIN_SOURCES).
#define BO1_NATIVE_POSIX_IMPL
#include "native_posix.h"
#include <sys/types.h>
#include <unistd.h>

static void Fill(const struct stat *st, struct bo1_stat *out)
{
    out->st_size = (int64_t)st->st_size;
    out->st_atim.tv_sec = st->st_atim.tv_sec;
    out->st_atim.tv_nsec = st->st_atim.tv_nsec;
    out->st_mtim.tv_sec = st->st_mtim.tv_sec;
    out->st_mtim.tv_nsec = st->st_mtim.tv_nsec;
    out->st_ctim.tv_sec = st->st_ctim.tv_sec;
    out->st_ctim.tv_nsec = st->st_ctim.tv_nsec;
    out->st_mode = (uint32_t)st->st_mode;
    out->reserved = 0;
}

int bo1_stat(const char *path, struct bo1_stat *out)
{
    struct stat st;
    const int r = stat(path, &st);
    if (r == 0)
        Fill(&st, out);
    return r;
}

int bo1_fstat(int fd, struct bo1_stat *out)
{
    struct stat st;
    const int r = fstat(fd, &st);
    if (r == 0)
        Fill(&st, out);
    return r;
}
