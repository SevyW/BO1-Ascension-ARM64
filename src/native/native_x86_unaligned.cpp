// native_x86_unaligned.cpp - x86 native builds only: make aligned SSE/AVX moves tolerate unaligned addresses.
//
// Why. The decompiled source spells every vector load and store as a typed dereference (`s_viewPos = *(float4 *)p`,
// struct copies of alignas(16) types, memcpy of __declspec(align(64)) structs). In the retail exe many of those
// were MOVUPS: job data sits in 4-aligned jqBatch storage, snapshots in 4-aligned hunk blocks. MSVC compiles the
// decompiled form with unaligned moves again; clang trusts the type and emits MOVAPS / MOVDQA, which raise #GP
// (SIGSEGV) on an address that is not 16-aligned. WebAssembly has no alignment faults and neither has AArch64 for
// NEON loads, so this is a property of the x86 test builds, not of the port's targets.
//
// What. cmake/native.cmake compiles x86 builds with -mavx: with VEX encoding, memory operands of arithmetic
// instructions need no alignment, so only the explicit aligned moves can fault. On SIGSEGV this handler decodes the
// faulting instruction; if it is one of those moves it rewrites it in place to the unaligned form (same length),
// logs the address once, and resumes. Anything else goes to the previous handler (a real crash stays a crash).
// Each site is patched on first use; `addr2line -e bo1 <address>` names the source line.
//
// The rewrite is one byte (the opcode, or the VEX/legacy prefix that selects the unaligned variant), so a thread
// executing the same instruction concurrently sees either the old or the new valid instruction.
#if defined(BO1_NATIVE) && (defined(__i386__) || defined(__x86_64__))

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <atomic>
#if !defined(__ANDROID__)
#include <execinfo.h>
#endif

namespace
{
struct sigaction s_previous;
std::atomic<int> s_patched{0};

bool WriteCode(uint8_t *at, uint8_t value)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t *base = (uint8_t *)((uintptr_t)at & ~(uintptr_t)(page - 1));
    if (mprotect(base, (size_t)page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    *at = value;
    mprotect(base, (size_t)page, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)at, (char *)at + 1);
    return true;
}

// Returns true if the instruction at ip was an aligned vector move and is now its unaligned twin.
bool PatchAlignedMove(uint8_t *ip)
{
    // ----- VEX encoded (what clang emits with -mavx) -----
    if (ip[0] == 0xC5 || ip[0] == 0xC4)
    {
#if defined(__i386__)
        // 32-bit: C4 / C5 are LES / LDS unless the next byte's top two bits are set (then it is a VEX prefix)
        if ((ip[1] & 0xC0) != 0xC0)
            return false;
#endif
        uint8_t *ppByte, *op;
        if (ip[0] == 0xC5)
        {
            ppByte = &ip[1];
            op = &ip[2];
        }
        else
        {
            if ((ip[1] & 0x1F) != 1)   // opcode map 0F only
                return false;
            ppByte = &ip[2];
            op = &ip[3];
        }
        const uint8_t pp = *ppByte & 3;   // 0: none (ps), 1: 66 (pd / dqa), 2: F3 (dqu)
        switch (*op)
        {
        case 0x28:   // VMOVAPS / VMOVAPD load  -> VMOVUPS / VMOVUPD
            return pp <= 1 && WriteCode(op, 0x10);
        case 0x29:   // VMOVAPS / VMOVAPD store
            return pp <= 1 && WriteCode(op, 0x11);
        case 0x2B:   // VMOVNTPS / VMOVNTPD     -> VMOVUPS / VMOVUPD store
            return pp <= 1 && WriteCode(op, 0x11);
        case 0x6F:   // VMOVDQA load            -> VMOVDQU (F3)
        case 0x7F:   // VMOVDQA store
            return pp == 1 && WriteCode(ppByte, (uint8_t)((*ppByte & ~3) | 2));
        default:
            return false;
        }
    }
    // ----- legacy SSE encoding (code built without AVX: only the plain moves can be fixed) -----
    uint8_t *p = ip;
    uint8_t *prefix66 = nullptr;
    if (*p == 0x66)
        prefix66 = p++;
#if defined(__x86_64__)
    if ((*p & 0xF0) == 0x40)   // REX
        ++p;
#endif
    if (p[0] != 0x0F)
        return false;
    switch (p[1])
    {
    case 0x28:   // MOVAPS / MOVAPD load -> MOVUPS / MOVUPD
        return WriteCode(&p[1], 0x10);
    case 0x29:
        return WriteCode(&p[1], 0x11);
    case 0x2B:   // MOVNTPS / MOVNTPD
        return WriteCode(&p[1], 0x11);
    case 0x6F:   // 66 0F 6F MOVDQA -> F3 0F 6F MOVDQU
    case 0x7F:
        return prefix66 && WriteCode(prefix66, 0xF3);
    default:
        return false;
    }
}

}

// The stack of a crash that is not an alignment fault: instruction, address touched, and the frames (through the
// signal frame, so the crashing function is in the list).
extern "C" __attribute__((noinline)) void bo1_native_fatal_signal(int sig, void *ip, void *address)
{
    char line[160];
    const int len = snprintf(line, sizeof(line), "native-x86: fatal signal %d at instruction %p touching %p\n", sig, ip,
        address);
    if (len > 0)
        (void)!write(2, line, (size_t)len);
#if !defined(__ANDROID__)
    void *frames[48];
    const int n = backtrace(frames, 48);
    backtrace_symbols_fd(frames, n, 2);
#endif
}

namespace
{
void OnSegv(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;
#if defined(__i386__)
    uint8_t *ip = (uint8_t *)uc->uc_mcontext.gregs[REG_EIP];
#else
    uint8_t *ip = (uint8_t *)uc->uc_mcontext.gregs[REG_RIP];
#endif
    // #GP from a misaligned operand arrives as SIGSEGV sent by the kernel with no fault address
    if (info && info->si_code == SI_KERNEL && ip && PatchAlignedMove(ip))
    {
        const int n = s_patched.fetch_add(1) + 1;
        char line[160];
        const int len = snprintf(line, sizeof(line),
            "native-x86: unaligned vector access at %p, instruction made unaligned (%d so far)\n", (void *)ip, n);
        if (len > 0)
            (void)!write(2, line, (size_t)len);
        return;   // retry the (now unaligned) instruction
    }
    // not ours: a real crash. Say where (a debugger that lets SIGSEGV through to this handler can break on
    // bo1_native_fatal_signal), then hand the signal to whoever was there before (default: die with a core).
    bo1_native_fatal_signal(sig, ip, info ? info->si_addr : nullptr);
    if (s_previous.sa_flags & SA_SIGINFO)
    {
        if (s_previous.sa_sigaction)
        {
            s_previous.sa_sigaction(sig, info, context);
            return;
        }
    }
    else if (s_previous.sa_handler != SIG_DFL && s_previous.sa_handler != SIG_IGN)
    {
        s_previous.sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
}

extern "C" void bo1_native_x86_install_unaligned_fixup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnSegv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_previous);
}

#elif defined(BO1_NATIVE) && defined(__aarch64__)

// AArch64: nothing to fix up (NEON loads take any alignment); the handler only reports a crash - instruction, address
// touched, and the frames by walking the frame records (x29), which needs no unwinder and so also works on Android.
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>

extern "C" __attribute__((noinline)) void bo1_native_fatal_signal(int sig, void *ip, void *address)
{
    char line[200];
    const int len = snprintf(line, sizeof(line), "native-arm64: fatal signal %d at instruction %p touching %p\n", sig,
        ip, address);
    if (len > 0)
        (void)!write(2, line, (size_t)len);
}

namespace
{
void PrintFrame(int index, uintptr_t pc)
{
    char line[400];
    Dl_info info;
    int len;
    if (dladdr((void *)pc, &info) && info.dli_sname)
        len = snprintf(line, sizeof(line), "  #%d %p %s+0x%lx\n", index, (void *)pc, info.dli_sname,
            (unsigned long)(pc - (uintptr_t)info.dli_saddr));
    else
        len = snprintf(line, sizeof(line), "  #%d %p\n", index, (void *)pc);
    if (len > 0)
        (void)!write(2, line, (size_t)(len < (int)sizeof(line) ? len : (int)sizeof(line) - 1));
}

// the line of /proc/self/maps an address is in (which library or region, and whether it may be written)
void PrintMapping(const char *what, uintptr_t address)
{
    static char text[1 << 16];
    const int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return;
    size_t have = 0;
    bool found = false;
    for (;;)
    {
        const ssize_t got = read(fd, text + have, sizeof(text) - 1 - have);
        if (got <= 0)
            break;
        have += (size_t)got;
        text[have] = 0;
        char *line = text;
        for (char *end; (end = strchr(line, '\n')) != nullptr; line = end + 1)
        {
            unsigned long long from = 0, to = 0;
            if (sscanf(line, "%llx-%llx", &from, &to) == 2 && address >= from && address < to)
            {
                char out[512];
                const int len = snprintf(out, sizeof(out), "  %s %p is in: %.*s\n", what, (void *)address, (int)(end - line), line);
                if (len > 0)
                    (void)!write(2, out, (size_t)(len < (int)sizeof(out) ? len : (int)sizeof(out) - 1));
                found = true;
                break;
            }
        }
        if (found)
            break;
        // keep the unfinished last line
        have = strlen(line);
        memmove(text, line, have);
    }
    close(fd);
    if (!found)
    {
        char out[120];
        const int len = snprintf(out, sizeof(out), "  %s %p is not mapped\n", what, (void *)address);
        if (len > 0)
            (void)!write(2, out, (size_t)len);
    }
}

void OnFatal(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;
    const uintptr_t pc = (uintptr_t)uc->uc_mcontext.pc;
    bo1_native_fatal_signal(sig, (void *)pc, info ? info->si_addr : nullptr);
    // the registers and where the addresses are: enough to read the crash from a log alone
    {
        char line[512];
        int len = 0;
        for (int r = 0; r < 31 && len < (int)sizeof(line) - 40; ++r)
            len += snprintf(line + len, sizeof(line) - (size_t)len, "%sx%d=%llx", r % 6 ? " " : "\n  ", r,
                (unsigned long long)uc->uc_mcontext.regs[r]);
        len += snprintf(line + len, sizeof(line) - (size_t)len, " sp=%llx\n", (unsigned long long)uc->uc_mcontext.sp);
        (void)!write(2, line, (size_t)len);
        PrintMapping("instruction", pc);
        PrintMapping("address", info ? (uintptr_t)info->si_addr : 0);
        PrintMapping("stack", (uintptr_t)uc->uc_mcontext.sp);
    }
    PrintFrame(0, pc);
    PrintFrame(1, (uintptr_t)uc->uc_mcontext.regs[30]);
    // frame records: {previous x29, return address}, each above the one before
    uintptr_t fp = (uintptr_t)uc->uc_mcontext.regs[29];
    for (int i = 2; i < 40 && fp && !(fp & 7); ++i)
    {
        const uintptr_t next = ((uintptr_t *)fp)[0];
        const uintptr_t ret = ((uintptr_t *)fp)[1];
        if (!ret)
            break;
        PrintFrame(i, ret);
        if (next <= fp)
            break;
        fp = next;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
}

extern "C" void bo1_native_x86_install_unaligned_fixup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnFatal;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

#else

extern "C" void bo1_native_x86_install_unaligned_fixup(void)
{
}

#endif


// ---------------------------------------------------------------------------------------------------------------
// TEST SWITCH (AArch64 Linux / Android): BO1_WATCH=<hex address>, or the file named by BO1_WATCH_FILE holding one.
// bo1_native_watch_arm() forks a child that attaches to every thread of this process with ptrace, sets a hardware
// write watchpoint of 4 bytes at the address and prints the program counter and link register of each write (at
// most 400) until bo1_native_watch_disarm(). For finding a stray write to a known address.
#if defined(__aarch64__) && defined(__linux__)
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <asm/ptrace.h>

namespace
{
int s_watchPipe[2] = {-1, -1}, s_watchGo[2] = {-1, -1};
pid_t s_watchChild = 0;

// (the child of a fork of a threaded process: no malloc, no stdio streams)
void WatchSay(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (length > 0)
        (void)!write(1, text, (size_t)(length < (int)sizeof(text) ? length : (int)sizeof(text) - 1));
}

unsigned long long WatchAddress()
{
    const char *text = getenv("BO1_WATCH");
    static char fromFile[64];
    if ((!text || !*text) && getenv("BO1_WATCH_FILE"))
    {
        const int f = open(getenv("BO1_WATCH_FILE"), O_RDONLY);
        if (f >= 0)
        {
            const long got = read(f, fromFile, sizeof(fromFile) - 1);
            close(f);
            if (got > 0)
            {
                fromFile[got] = 0;
                text = fromFile;
            }
        }
    }
    return text && *text ? strtoull(text, nullptr, 16) : 0;
}

bool SetWatch(pid_t tid, unsigned long long address, bool on)
{
    struct user_hwdebug_state state;
    memset(&state, 0, sizeof(state));
    struct iovec io = {&state, sizeof(state)};
    if (ptrace(PTRACE_GETREGSET, tid, (void *)NT_ARM_HW_WATCH, &io) != 0)
        return false;
    const unsigned offset = (unsigned)(address & 7);
    state.dbg_regs[0].addr = address & ~7ull;
    // enable, EL0, writes, byte address select: the 4 bytes at the address
    state.dbg_regs[0].ctrl = on ? (1u | (2u << 1) | (2u << 3) | (((0xFu << offset) & 0xFFu) << 5)) : 0;
    io.iov_len = offsetof(struct user_hwdebug_state, dbg_regs) + sizeof(state.dbg_regs[0]);
    return ptrace(PTRACE_SETREGSET, tid, (void *)NT_ARM_HW_WATCH, &io) == 0;
}

void WatchChild(pid_t parent, unsigned long long address)
{
    char go = 0;
    (void)!read(s_watchGo[0], &go, 1);   // the parent has named this process as its tracer
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", (int)parent);
    static pid_t tids[256];
    int count = 0;
    const int dir = open(path, O_RDONLY | O_DIRECTORY);
    static char entries[16384];
    for (;;)
    {
        const long got = dir >= 0 ? syscall(SYS_getdents64, dir, entries, sizeof(entries)) : 0;
        if (got <= 0)
            break;
        for (long at = 0; at < got;)
        {
            // struct linux_dirent64: ino (8), off (8), reclen (2), type (1), name
            const unsigned short length = *(const unsigned short *)(entries + at + 16);
            const pid_t tid = (pid_t)atoi(entries + at + 19);
            at += length;
            if (tid <= 0 || count >= 256)
                continue;
            if (ptrace(PTRACE_SEIZE, tid, 0, 0) != 0)
            {
                WatchSay("watch: thread %d: no attach (%d)\n", (int)tid, errno);
                continue;
            }
            ptrace(PTRACE_INTERRUPT, tid, 0, 0);
            int status = 0;
            waitpid(tid, &status, __WALL);
            if (!SetWatch(tid, address, true))
                WatchSay("watch: thread %d: the watchpoint could not be set\n", (int)tid);
            ptrace(PTRACE_CONT, tid, 0, 0);
            tids[count++] = tid;
        }
    }
    if (dir < 0)
        WatchSay("watch: %s does not open (%d)\n", path, errno);
    if (dir >= 0)
        close(dir);
    WatchSay("watch: %d threads traced, address %llx\n", count, address);
    char ready = 1;
    (void)!write(s_watchPipe[1], &ready, 1);
    int hits = 0;
    for (;;)
    {
        int status = 0;
        const pid_t tid = waitpid(-1, &status, __WALL);
        if (tid <= 0)
            break;
        if (!WIFSTOPPED(status))
            continue;
        const int sig = WSTOPSIG(status);
        if (sig == SIGTRAP && (status >> 16) == 0)
        {
            struct user_pt_regs regs;
            struct iovec io = {&regs, sizeof(regs)};
            ptrace(PTRACE_GETREGSET, tid, (void *)NT_PRSTATUS, &io);
            static unsigned long long lastX19 = ~0ull;
            const bool changed = regs.regs[19] != lastX19;
            lastX19 = regs.regs[19];
            if (hits++ < 60 || (changed && hits < 4000))
                WatchSay("watch: write by thread %d at pc %llx lr %llx sp %llx x19 %llx\n", (int)tid, (unsigned long long)regs.pc,
                    (unsigned long long)regs.regs[30], (unsigned long long)regs.sp, (unsigned long long)regs.regs[19]);
            // step over the write with the watchpoint off
            SetWatch(tid, address, false);
            ptrace(PTRACE_SINGLESTEP, tid, 0, 0);
            waitpid(tid, &status, __WALL);
            SetWatch(tid, address, true);
            ptrace(PTRACE_CONT, tid, 0, 0);
        }
        else if (sig == SIGUSR2)
        {
            // the parent's "stop watching"
            for (int i = 0; i < count; ++i)
            {
                if (tids[i] != tid)
                {
                    ptrace(PTRACE_INTERRUPT, tids[i], 0, 0);
                    waitpid(tids[i], &status, __WALL);
                }
                SetWatch(tids[i], address, false);
                ptrace(PTRACE_DETACH, tids[i], 0, 0);
            }
            WatchSay("watch: done, %d writes\n", hits);
            _exit(0);
        }
        else
        {
            // any other stop: pass the signal on (events and group stops: nothing to pass)
            ptrace(PTRACE_CONT, tid, 0, (status >> 16) ? 0 : (sig == SIGTRAP ? 0 : sig));
        }
    }
    _exit(0);
}
}

extern "C" void bo1_native_watch_arm(void)
{
    const unsigned long long address = WatchAddress();
    if (!address || s_watchChild)
        return;
    fflush(stdout);
    fflush(stderr);
    if (pipe(s_watchPipe) != 0 || pipe(s_watchGo) != 0)
        return;
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);   // (an app that is not debuggable is not: no /proc/<pid>/task, no attach)
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child == 0)
    {
        WatchChild(parent, address);
        _exit(0);
    }
    if (child < 0)
        return;
    prctl(PR_SET_PTRACER, child, 0, 0, 0);
    signal(SIGUSR2, SIG_IGN);   // (a traced thread still stops for it; untraced, nothing happens)
    s_watchChild = child;
    char go = 1;
    (void)!write(s_watchGo[1], &go, 1);
    char ready = 0;
    (void)!read(s_watchPipe[0], &ready, 1);
}

extern "C" void bo1_native_watch_disarm(void)
{
    if (!s_watchChild)
        return;
    raise(SIGUSR2);   // stops this thread; the child sees it, detaches from every thread and exits
    int status = 0;
    waitpid(s_watchChild, &status, 0);
    s_watchChild = 0;
}
#else
extern "C" void bo1_native_watch_arm(void) {}
extern "C" void bo1_native_watch_disarm(void) {}
#endif
