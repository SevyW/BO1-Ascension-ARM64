// native_lowmem.c - keeps the 64-bit build's memory below 4 GB (the 32-bit-pointer build, tools/p32).
//
// In that build every pointer object the engine declares is 4 bytes, zero-extended. That is only correct while
// everything the engine can point at has an address below 2^32:
//   - the program image: linked non-PIE at its default low address (cmake/native.cmake, -no-pie);
//   - the heap: malloc and friends are defined here, over one region reserved below 4 GB (dlmalloc mspace). On
//     glibc that replaces the allocator for the whole process, shared libraries included;
//   - thread stacks: the engine's threads get stacks from that heap (bo1_lowmem_thread_stack, used by the Win32
//     emulation), and main() moves the engine's own main function onto such a thread (bo1_lowmem_run), because the
//     engine stores addresses of locals in its structures all the time.
// What stays outside: the host's own static storage and stacks (the C library's FILE objects are heap, hence low;
// getenv strings, strerror text and OpenGL strings are not). Code that receives such a pointer from the host must
// keep it in a native pointer (bo1_hostptr.h).
//
// Environment: BO1_LOWMEM_BASE / BO1_LOWMEM_MB override the region (defaults 0x40000000 and 3056 MB).
//
// Android is different in three ways (src/native/android/bo1_boot.c):
//   - the engine is a shared library, loaded into a range below 4 GB that the loader stub reserved; the stub also
//     reserves the heap's range (the largest free one: an app's process already has the Java heap down there) and
//     passes it in BO1_LOWMEM_BASE / BO1_LOWMEM_MB with BO1_LOWMEM_RESERVED=1;
//   - malloc here is this library's only (linked -Bsymbolic, with libc++ and the compiler runtime static, so
//     operator new and emulated thread-locals come from it too). The C library, SDL and the GL driver keep
//     bionic's allocator, whose blocks are far above 4 GB (and tagged in the top byte): a block from them is
//     handed back to bionic by free/realloc here, and the libc functions that return allocated memory to the
//     engine (strdup, ...) are defined here as well;
//   - nothing else of the process is low: every pointer from the system must stay in a native pointer.
//
// This file is not rewritten and is compiled without the engine's ABI flags.
#if defined(BO1_NATIVE) && defined(__LP64__)

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif

// ----- dlmalloc, mspace only, over memory this file supplies -----
#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_LOCKS 1
#define USE_SPIN_LOCKS 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define NO_MALLINFO 1
#define MALLOC_ALIGNMENT ((size_t)16U)
#define ABORT_ON_ASSERT_FAILURE 0
#define LACKS_TIME_H 1
#define NO_MALLOC_STATS 1
#include "../../third_party/dlmalloc/malloc.c"

static mspace s_space;
static volatile int s_initLock;
static uintptr_t s_base, s_end;

static void LowmemFail(const char *what)
{
    static const char prefix[] = "native: low memory: ";
    (void)!write(2, prefix, sizeof(prefix) - 1);
    (void)!write(2, what, strlen(what));
    (void)!write(2, "\n", 1);
    _exit(70);
}

static unsigned long EnvNumber(const char *name, unsigned long fallback)
{
    // getenv / strtoul do not allocate
    const char *v = getenv(name);
    if (!v || !*v)
        return fallback;
    return strtoul(v, NULL, 0);
}

static void LowmemInit(void)
{
    while (__sync_lock_test_and_set(&s_initLock, 1))
        ;
    if (!s_space)
    {
        // (Linux: from 1 GB to just under 4 GB. A multiplayer match with Mesa's software renderer, whose textures
        // are in this heap too, passes 2.5 GB.)
        const uintptr_t base = EnvNumber("BO1_LOWMEM_BASE", 0x40000000ul);
        const size_t size = (size_t)EnvNumber("BO1_LOWMEM_MB", 3056) << 20;
        if (base + size > 0x100000000ull)
            LowmemFail("BO1_LOWMEM_BASE + BO1_LOWMEM_MB reaches past 4 GB");
#ifdef __ANDROID__
        if (!getenv("BO1_LOWMEM_BASE"))
            LowmemFail("no heap range (libbo1.so must be loaded by the bo1_boot stub)");
#endif
        // reserved once, committed by the kernel as pages are touched. A range the loader stub holds (PROT_NONE)
        // is mapped over; otherwise the range must be free.
        const int fixed = EnvNumber("BO1_LOWMEM_RESERVED", 0) ? MAP_FIXED : MAP_FIXED_NOREPLACE;
        void *p = mmap((void *)base, size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | fixed, -1, 0);
        if (p == MAP_FAILED || (uintptr_t)p != base)
            LowmemFail("could not reserve the region below 4 GB (is something mapped there?)");
        s_base = base;
        s_end = base + size;
        s_space = create_mspace_with_base(p, size, 1);
        if (!s_space)
            LowmemFail("could not create the heap");
    }
    __sync_lock_release(&s_initLock);
}

static inline mspace Space(void)
{
    if (__builtin_expect(!s_space, 0))
        LowmemInit();
    return s_space;
}

// live blocks by size class (16 bytes .. 2 GB in powers of two), for the report when the heap runs out
static long s_liveBlocks[32];
static long long s_liveBytes[32];

static inline int SizeClass(size_t size)
{
    int c = 0;
    while (c < 31 && ((size_t)16 << c) < size)
        ++c;
    return c;
}

static inline void Account(void *p, int sign)
{
    if (p)
    {
        const size_t size = mspace_usable_size(p);
        const int c = SizeClass(size);
        __sync_fetch_and_add(&s_liveBlocks[c], sign);
        __sync_fetch_and_add(&s_liveBytes[c], sign * (long long)size);
    }
}

#if !defined(__ANDROID__)
#include <execinfo.h>
#endif

static void *OutOfMemory(size_t size)
{
    char line[160];
    for (int c = 0; c < 32; ++c)
    {
        if (s_liveBytes[c] < (8 << 20))
            continue;
        const int n = snprintf(line, sizeof(line), "native: low memory: %ld live blocks of up to %zu bytes: %lld MB\n",
            s_liveBlocks[c], (size_t)16 << c, s_liveBytes[c] >> 20);
        (void)!write(2, line, (size_t)n);
    }
#if !defined(__ANDROID__)
    void *frames[24];
    backtrace_symbols_fd(frames, backtrace(frames, 24), 2);
#endif
    snprintf(line, sizeof(line), "out of memory below 4 GB (request of %zu bytes)", size);
    LowmemFail(line);
    return NULL;
}

static inline int IsOurs(const void *p)
{
    return (uintptr_t)p >= s_base && (uintptr_t)p < s_end;
}

#ifdef __ANDROID__
// bionic's own allocator, for the blocks that came from it (dlsym: the names here are this library's)
static void (*s_hostFree)(void *);
static size_t (*s_hostUsableSize)(const void *);
static void HostInit(void)
{
    if (!s_hostFree)
    {
        s_hostUsableSize = (size_t (*)(const void *))dlsym(RTLD_NEXT, "malloc_usable_size");
        s_hostFree = (void (*)(void *))dlsym(RTLD_NEXT, "free");
    }
}
#endif

// ----- Linux: a second heap, above 4 GB, for the graphics stack -----
// On glibc the functions below are the whole process's allocator, so DXVK, the Vulkan driver and (with Mesa's
// software drivers) LLVM's compiler would fill the low heap: DXVK builds every shader's pipeline when the shader is
// created, and with lavapipe that alone passed 2 GB in the front end. Nothing they allocate is pointed at by the
// engine (the Vulkan bridge copies locked memory, src/web/d3d9/d3d9_vkbridge.cpp), so calls that come from those
// libraries are served from a heap of their own. A caller is recognized by its return address:
// bo1_lowmem_scan_host_modules records the address ranges of the loaded libraries whose names contain one of
// s_hostNames (called by the Vulkan side after it loads DXVK and again after the driver is in).
// Android does not need it: only libbo1.so uses this allocator there.
#if !defined(__ANDROID__)
#include <link.h>

#define HOST_MODULES 24
#define HOST_HEAP_BYTES ((size_t)24 << 30)   // address space only: committed as it is touched
static const char *const s_hostNames[] = { "dxvk", "vulkan", "LLVM", "libstdc++", "libdrm", "libnvidia", NULL };
static struct
{
    uintptr_t lo, hi;
} s_hostModule[HOST_MODULES];
static volatile int s_hostModules;
static mspace s_hostSpace;
static uintptr_t s_hostBase, s_hostEnd;

static int ScanModule(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    const char *name = info->dlpi_name;
    int wanted = 0;
    for (int i = 0; name && s_hostNames[i]; ++i)
        wanted |= strstr(name, s_hostNames[i]) != NULL;
    if (!wanted)
        return 0;
    uintptr_t lo = ~(uintptr_t)0, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD)
            continue;
        const uintptr_t begin = info->dlpi_addr + ph->p_vaddr, end = begin + ph->p_memsz;
        if (begin < lo)
            lo = begin;
        if (end > hi)
            hi = end;
    }
    if (hi <= lo)
        return 0;
    for (int i = 0; i < s_hostModules; ++i)
        if (s_hostModule[i].lo == lo)
            return 0;
    if (s_hostModules < HOST_MODULES)
    {
        s_hostModule[s_hostModules].lo = lo;
        s_hostModule[s_hostModules].hi = hi;
        __sync_fetch_and_add(&s_hostModules, 1);
    }
    return 0;
}

void bo1_lowmem_scan_host_modules(void)
{
    while (__sync_lock_test_and_set(&s_initLock, 1))
        ;
    if (!s_hostSpace)
    {
        void *p = mmap(NULL, HOST_HEAP_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p != MAP_FAILED)
        {
            s_hostBase = (uintptr_t)p;
            s_hostEnd = s_hostBase + HOST_HEAP_BYTES;
            s_hostSpace = create_mspace_with_base(p, HOST_HEAP_BYTES, 1);
        }
    }
    if (s_hostSpace)
        dl_iterate_phdr(ScanModule, NULL);
    __sync_lock_release(&s_initLock);
}

static inline int FromHost(const void *caller)
{
    const int count = s_hostModules;
    for (int i = 0; i < count; ++i)
        if ((uintptr_t)caller >= s_hostModule[i].lo && (uintptr_t)caller < s_hostModule[i].hi)
            return 1;
    return 0;
}

static inline int IsHostBlock(const void *p)
{
    return (uintptr_t)p >= s_hostBase && (uintptr_t)p < s_hostEnd;
}
#define CALLER() __builtin_return_address(0)
#else
void bo1_lowmem_scan_host_modules(void)
{
}
#define FromHost(caller) 0
#define IsHostBlock(p) 0
#define CALLER() NULL
#define s_hostSpace NULL
#endif

// ----- the C allocation interface -----
void *malloc(size_t size)
{
    if (FromHost(CALLER()))
        return mspace_malloc(s_hostSpace, size);
    void *p = mspace_malloc(Space(), size);
    Account(p, 1);
    return p || !size ? p : OutOfMemory(size);
}

void free(void *p)
{
    if (!p)
        return;
    if (IsHostBlock(p))
    {
        mspace_free(s_hostSpace, p);
        return;
    }
    if (!IsOurs(p))
    {
#ifdef __ANDROID__
        HostInit();
        if (s_hostFree)
            s_hostFree(p);
#endif
        // (Linux: a block from before this allocator existed, the dynamic loader's own, is not ours to free)
        return;
    }
    Account(p, -1);
    mspace_free(s_space, p);
}

void *calloc(size_t count, size_t size)
{
    if (FromHost(CALLER()))
        return mspace_calloc(s_hostSpace, count, size);
    void *p = mspace_calloc(Space(), count, size);
    Account(p, 1);
    return p || !count || !size ? p : OutOfMemory(count * size);
}

void *realloc(void *old, size_t size)
{
    if (old ? IsHostBlock(old) : FromHost(CALLER()))
        return mspace_realloc(s_hostSpace, old, size);
#ifdef __ANDROID__
    if (old && !IsOurs(old))
    {
        // a block of bionic's: move it here
        HostInit();
        const size_t have = s_hostUsableSize ? s_hostUsableSize(old) : 0;
        void *p = malloc(size);
        if (p)
            memcpy(p, old, have < size ? have : size);
        if (s_hostFree)
            s_hostFree(old);
        return p;
    }
#endif
    if (old && !IsOurs(old))
    {
        // foreign block of unknown size: cannot be grown, and nothing here should ever hold one
        LowmemFail("realloc of a block that is not from the low heap");
    }
    Account(old, -1);
    void *p = mspace_realloc(Space(), old, size);
    Account(p ? p : old, 1);
    return p || !size ? p : OutOfMemory(size);
}

void *memalign(size_t alignment, size_t size)
{
    if (FromHost(CALLER()))
        return mspace_memalign(s_hostSpace, alignment, size);
    void *p = mspace_memalign(Space(), alignment, size);
    Account(p, 1);
    return p || !size ? p : OutOfMemory(size);
}

void *aligned_alloc(size_t alignment, size_t size)
{
    if (FromHost(CALLER()))
        return mspace_memalign(s_hostSpace, alignment, size);
    void *p = mspace_memalign(Space(), alignment, size);
    Account(p, 1);
    return p || !size ? p : OutOfMemory(size);
}

int posix_memalign(void **out, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    if (FromHost(CALLER()))
    {
        void *h = mspace_memalign(s_hostSpace, alignment, size);
        if (!h && size)
            return ENOMEM;
        *out = h;
        return 0;
    }
    void *p = mspace_memalign(Space(), alignment, size);
    Account(p, 1);
    if (!p && size)
        return ENOMEM;
    *out = p;
    return 0;
}

void *valloc(size_t size)
{
    return memalign((size_t)sysconf(_SC_PAGESIZE), size);
}

void *pvalloc(size_t size)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    return memalign(page, (size + page - 1) & ~(page - 1));
}

#ifdef __ANDROID__
size_t malloc_usable_size(const void *p)
{
    if (!p)
        return 0;
    if (!IsOurs(p))
    {
        HostInit();
        return s_hostUsableSize ? s_hostUsableSize(p) : 0;
    }
    return mspace_usable_size(p);
}

// libc functions that return memory the caller frees: from this heap, so that the engine can keep the pointer
char *strdup(const char *text)
{
    const size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

char *strndup(const char *text, size_t count)
{
    const size_t length = strnlen(text, count);
    char *copy = (char *)malloc(length + 1);
    if (copy)
    {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}
#else
size_t malloc_usable_size(void *p)
{
    if (p && IsHostBlock(p))
        return mspace_usable_size(p);
    if (!p || !IsOurs(p))
        return 0;
    return mspace_usable_size(p);
}
#endif

// ----- thread stacks -----
// A stack for a thread the engine creates. Never freed: the engine's threads live as long as the process.
void *bo1_lowmem_thread_stack(size_t size)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size = (size + page - 1) & ~(page - 1);
    void *stack = memalign(page, size + page);
    // a guard page at the low end: an overflow faults instead of walking into the heap
    mprotect(stack, page, PROT_NONE);
    return (char *)stack + page;
}

struct RunArgs
{
    int (*function)(int, char **);
    int argc;
    char **argv;
    int result;
};

static void *RunThread(void *arg)
{
    struct RunArgs *run = (struct RunArgs *)arg;
    run->result = run->function(run->argc, run->argv);
    return NULL;
}

// Runs function(argc, argv) on a thread whose stack is below 4 GB and returns its result.
int bo1_lowmem_run(int (*function)(int, char **), int argc, char **argv, size_t stackSize)
{
    struct RunArgs run = {function, argc, argv, 0};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, bo1_lowmem_thread_stack(stackSize), stackSize);
    pthread_t thread;
    const int err = pthread_create(&thread, &attr, RunThread, &run);
    pthread_attr_destroy(&attr);
    if (err)
        LowmemFail("could not start the engine's main thread");
    pthread_join(thread, NULL);
    return run.result;
}

// 1 if the address is something a 32-bit pointer can hold
int bo1_lowmem_is_low(const void *address)
{
    return (uintptr_t)address < 0x100000000ull;
}

#endif
