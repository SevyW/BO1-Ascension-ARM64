// win_crt.cpp - the MSVC C runtime extensions declared in bo1_web_prelude.h, over musl (web build).
#include "bo1_win_internal.h"
#include "bo1_platform.h"
#ifndef BO1_NATIVE
#include <emscripten/em_asm.h>
#endif
#include <sys/stat.h>
#include <atomic>
#include <malloc.h>
#include <sys/time.h>
#include <unistd.h>

// ----- fatal / lifecycle (docs/web-engine-interface.md section 5) -----
// The page's callbacks live on the Module object of the browser main thread: the hooks run there (MAIN_THREAD_EM_ASM
// blocks the calling engine thread until they ran).
extern "C" void bo1_web_report_error(const char *message)
{
#ifdef BO1_NATIVE
    // native: there is no page; the message goes to stderr
    fprintf(stderr, "ENGINE ERROR: %s\n", message ? message : "");
    fflush(stderr);
#else
    MAIN_THREAD_EM_ASM({
        try {
            if (Module['onEngineError']) Module['onEngineError'](UTF8ToString($0));
        } catch (e) {
            console.error(e);
        }
    }, message);
#endif
}

static std::atomic<int> s_exitNotified{0};
static int s_exitCode;

static void NotifyExit()
{
    if (s_exitNotified.exchange(1))
        return;
#ifndef BO1_NATIVE
    MAIN_THREAD_EM_ASM({
        try {
            if (Module['onEngineExit']) Module['onEngineExit']($0);
        } catch (e) {
            console.error(e);
        }
    }, s_exitCode);
#endif
}

__attribute__((constructor)) static void RegisterExitHook()
{
    atexit(NotifyExit);   // exit() calls the engine makes directly (s_exitCode stays 0)
}

// quit: tell the page, then end every engine thread (EXIT_RUNTIME)
#ifdef BO1_NATIVE
extern "C" void bo1_native_restart_now(void);
#endif

extern "C" __attribute__((noreturn)) void bo1_web_exit(int code)
{
    s_exitCode = code;
    NotifyExit();
    fflush(stdout);
    fflush(stderr);
#ifdef BO1_NATIVE
    // native: a quit that is a switch to the program's other mode or to another mod starts the program again
    // (src/native/native_extras.cpp); it returns only when there was nothing to start
    if (code == 0)
        bo1_native_restart_now();
#endif
    // like ExitProcess: no static destructors while the other engine threads still run (exit() would run them and the
    // threads then call into torn-down objects)
    _Exit(code);
}

extern "C" __attribute__((noreturn)) void bo1_web_debugbreak(const char *file, int line)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "__debugbreak at %s:%d (an assert or fatal check failed; Windows would crash here)", file, line);
#ifndef BO1_NATIVE
    fprintf(stderr, "%s\n", msg);
#endif
    bo1_plat_log_stack(msg);
    bo1_web_report_error(msg);
    abort();
}

// ----- C library functions that return static storage: copies in this program's own (low) memory -----
#if defined(BO1_NATIVE) && defined(__LP64__)
#undef localtime
#undef gmtime
#undef asctime
#undef ctime
#undef strerror
#undef getenv
#include <pthread.h>
extern "C" struct tm *bo1_host_localtime(const time_t *t)
{
    static struct tm result;
    return localtime_r(t, &result);
}

extern "C" struct tm *bo1_host_gmtime(const time_t *t)
{
    static struct tm result;
    return gmtime_r(t, &result);
}

extern "C" char *bo1_host_asctime(const struct tm *tm)
{
    static char text[64];
    return asctime_r(tm, text);
}

extern "C" char *bo1_host_ctime(const time_t *t)
{
    static char text[64];
    return ctime_r(t, text);
}

extern "C" char *bo1_host_strerror(int error)
{
    static char text[256];
    auto message = strerror(error);   // the C library's own text, wherever it lives
    snprintf(text, sizeof(text), "%s", message ? message : "");
    return text;
}

// The environment strings sit on the process's initial stack. Each name looked up gets one copy here, refreshed on
// every call (a variable can change), never freed (the engine looks up a handful of names).
extern "C" char *bo1_host_getenv(const char *name)
{
    struct Entry
    {
        char name[64];
        char value[1024];
    };
    static Entry entries[64];
    static int count;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    auto value = getenv(name);
    if (!value)
        return nullptr;
    pthread_mutex_lock(&lock);
    Entry *slot = nullptr;
    for (int i = 0; i < count && !slot; ++i)
        if (!strcmp(entries[i].name, name))
            slot = &entries[i];
    if (!slot)
        slot = &entries[count < 64 ? count++ : 63];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    snprintf(slot->value, sizeof(slot->value), "%s", value);
    pthread_mutex_unlock(&lock);
    return slot->value;
}
#endif

// ----- rand: MSVC's linear congruential generator, state per thread -----
static thread_local unsigned int t_randState = 1;

extern "C" int bo1_msvc_rand(void)
{
    t_randState = t_randState * 214013u + 2531011u;
    return (int)((t_randState >> 16) & 0x7FFFu);
}

extern "C" void bo1_msvc_srand(unsigned int seed)
{
    t_randState = seed;
}

// ----- strings -----
extern "C" {

int bo1_memicmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i)
    {
        const int d = tolower(x[i]) - tolower(y[i]);
        if (d)
            return d;
    }
    return 0;
}

char *_strlwr(char *s)
{
    for (char *p = s; *p; ++p)
        *p = (char)tolower((unsigned char)*p);
    return s;
}

char *_strupr(char *s)
{
    for (char *p = s; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
    return s;
}

char *_strrev(char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n / 2; ++i)
    {
        char c = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = c;
    }
    return s;
}

static char *UnsignedToA(unsigned long long v, char *buf, int radix, bool negative)
{
    char tmp[72];
    int n = 0;
    if (radix < 2 || radix > 36)
        radix = 10;
    do
    {
        const int d = (int)(v % (unsigned)radix);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= (unsigned)radix;
    } while (v);
    char *p = buf;
    if (negative)
        *p++ = '-';
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return buf;
}

char *_itoa(int value, char *buf, int radix)
{
    if (radix == 10 && value < 0)
        return UnsignedToA((unsigned long long)(-(long long)value), buf, radix, true);
    return UnsignedToA((unsigned int)value, buf, radix, false);
}

char *_ltoa(long value, char *buf, int radix)
{
    return _itoa((int)value, buf, radix);
}

char *_ultoa(unsigned long value, char *buf, int radix)
{
    return UnsignedToA(value, buf, radix, false);
}

char *_i64toa(long long value, char *buf, int radix)
{
    if (radix == 10 && value < 0)
        return UnsignedToA(0ull - (unsigned long long)value, buf, radix, true);
    return UnsignedToA((unsigned long long)value, buf, radix, false);
}

char *_ui64toa(unsigned long long value, char *buf, int radix)
{
    return UnsignedToA(value, buf, radix, false);
}

int bo1_msvc_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    const int len = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len < 0)
        return -1;
    if ((size_t)len < count)
        return vsnprintf(buf, count, fmt, ap);
    if (count)
    {
        // MSVC: the first count characters, no terminator
        char *tmp = (char *)malloc((size_t)len + 1);
        if (tmp)
        {
            vsnprintf(tmp, (size_t)len + 1, fmt, ap);
            memcpy(buf, tmp, count);
            free(tmp);
        }
    }
    return (size_t)len == count ? len : -1;
}

int bo1_msvc_snprintf(char *buf, size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = bo1_msvc_vsnprintf(buf, count, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (!buf || !size)
        return -1;
    const int r = vsnprintf(buf, size, fmt, ap);
    if (r < 0 || (size_t)r >= size)
    {
        buf[0] = 0;   // MSVC: the invalid parameter handler; the buffer is emptied
        return -1;
    }
    return r;
}

int sprintf_s(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = vsprintf_s(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int _vsnprintf_s(char *buf, size_t size, size_t count, const char *fmt, va_list ap)
{
    if (!buf || !size)
        return -1;
    const size_t limit = count == _TRUNCATE || count >= size ? size - 1 : count;
    va_list copy;
    va_copy(copy, ap);
    const int len = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len < 0)
    {
        buf[0] = 0;
        return -1;
    }
    if ((size_t)len <= limit)
        return vsnprintf(buf, size, fmt, ap);
    if (count == _TRUNCATE)
    {
        vsnprintf(buf, size, fmt, ap);   // truncated, terminated
        return -1;
    }
    if (count < size)
    {
        // count characters fit: they are written and terminated
        vsnprintf(buf, count + 1, fmt, ap);
        return -1;
    }
    buf[0] = 0;
    return -1;
}

int _snprintf_s(char *buf, size_t size, size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = _vsnprintf_s(buf, size, count, fmt, ap);
    va_end(ap);
    return r;
}

int strcpy_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size)
        return EINVAL;
    if (!src)
    {
        dst[0] = 0;
        return EINVAL;
    }
    const size_t n = strlen(src);
    if (n >= size)
    {
        dst[0] = 0;
        return ERANGE;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

int strncpy_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!dst || !size)
        return EINVAL;
    if (!src)
    {
        dst[0] = 0;
        return EINVAL;
    }
    size_t n = strnlen(src, count == _TRUNCATE ? size - 1 : count);
    if (n >= size)
    {
        if (count == _TRUNCATE)
            n = size - 1;
        else
        {
            dst[0] = 0;
            return ERANGE;
        }
    }
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

int strcat_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size || !src)
        return EINVAL;
    const size_t d = strnlen(dst, size);
    if (d == size)
        return EINVAL;
    return strcpy_s(dst + d, size - d, src);
}

int strncat_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!dst || !size || !src)
        return EINVAL;
    const size_t d = strnlen(dst, size);
    if (d == size)
        return EINVAL;
    return strncpy_s(dst + d, size - d, src, count);
}

int memcpy_s(void *dst, size_t size, const void *src, size_t count)
{
    if (count > size)
        return ERANGE;
    memcpy(dst, src, count);
    return 0;
}

int memmove_s(void *dst, size_t size, const void *src, size_t count)
{
    if (count > size)
        return ERANGE;
    memmove(dst, src, count);
    return 0;
}

int fopen_s(FILE **fp, const char *name, const char *mode)
{
    if (!fp)
        return EINVAL;
    *fp = fopen(name, mode);
    return *fp ? 0 : errno;
}

int _dupenv_s(char **buf, size_t *len, const char *name)
{
    const char *v = getenv(name);
    if (buf)
        *buf = v ? strdup(v) : nullptr;
    if (len)
        *len = v ? strlen(v) + 1 : 0;
    return 0;
}

// ----- time -----
char *_strtime(char *buf)
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, 9, "%H:%M:%S", &tm);
    return buf;
}

char *_strdate(char *buf)
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, 9, "%m/%d/%y", &tm);
    return buf;
}

void _ftime64(struct __timeb64 *tb)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    tb->time = tv.tv_sec;
    tb->millitm = (unsigned short)(tv.tv_usec / 1000);
    tb->timezone = 0;
    tb->dstflag = 0;
}

// ----- memory -----
// _aligned_malloc blocks carry their size in front (for _aligned_realloc / _aligned_msize):
//   [padding][size_t size][void *raw]  <- user pointer
void *_aligned_malloc(size_t size, size_t alignment)
{
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    const size_t header = sizeof(size_t) + sizeof(void *);
    char *raw = (char *)malloc(size + alignment + header);
    if (!raw)
        return nullptr;
    uintptr_t user = ((uintptr_t)raw + header + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void **)user)[-1] = raw;
    ((size_t *)(user - sizeof(void *)))[-1] = size;
    return (void *)user;
}

void _aligned_free(void *p)
{
    if (p)
        free(((void **)p)[-1]);
}

size_t _aligned_msize(void *p, size_t alignment, size_t offset)
{
    return p ? ((size_t *)((uintptr_t)p - sizeof(void *)))[-1] : 0;
}

void *_aligned_realloc(void *p, size_t size, size_t alignment)
{
    if (!p)
        return _aligned_malloc(size, alignment);
    if (!size)
    {
        _aligned_free(p);
        return nullptr;
    }
    void *n = _aligned_malloc(size, alignment);
    if (!n)
        return nullptr;
    const size_t old = _aligned_msize(p, alignment, 0);
    memcpy(n, p, old < size ? old : size);
    _aligned_free(p);
    return n;
}

size_t _msize(void *p)
{
    return p ? malloc_usable_size(p) : 0;
}

// ----- intrinsics -----
unsigned long long bo1_web_rdtsc(void)
{
    return bo1w::NowNs();
}

void bo1_web_cpuid(int info[4], int leaf)
{
    // a generic SSE2-era x86 ("GenuineIntel", family 6): what Sys_SupportsSSE-style checks want to see
    info[0] = info[1] = info[2] = info[3] = 0;
    if (leaf == 0)
    {
        info[0] = 1;
        memcpy(&info[1], "Genu", 4);
        memcpy(&info[3], "ineI", 4);
        memcpy(&info[2], "ntel", 4);
    }
    else if (leaf == 1)
    {
        info[0] = 0x000006F0;
        info[3] = (1 << 25) | (1 << 26);   // SSE, SSE2
    }
}

// ----- io.h / direct.h / process.h numbers -----
int _getpid(void)
{
    return 4;
}

long _filelength(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 ? (long)st.st_size : -1;
}

long long _filelengthi64(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 ? (long long)st.st_size : -1;
}

int _fileno(FILE *fp)
{
    return fileno(fp);
}

} // extern "C"
