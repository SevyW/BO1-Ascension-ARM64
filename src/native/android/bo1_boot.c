// bo1_boot.c - Android: loads the engine (libbo1.so) below 4 GB and starts it.
//
// The engine is the 32-bit-pointer build (tools/p32): every pointer object it declares is 4 bytes, so its code, its
// data, its heap and its stacks must all have addresses below 2^32. Android's linker places a library wherever its
// address space randomization says (far above), unless android_dlopen_ext is told to use a range the caller reserved
// (ANDROID_DLEXT_RESERVED_ADDRESS). This file is that caller. It is ordinary 64-bit code, built into:
//   - libmain.so, the library SDL's Java side (SDLActivity) runs: SDL_main below;
//   - bo1_boot_test (-DBO1_BOOT_TEST), a plain executable for trying the engine library without an app, e.g. the
//     dedicated server under qemu-user with a system image's bionic:  bo1_boot_test <libbo1.so> [engine arguments].
//
// What it does:
//   1. reads /proc/self/maps for the free ranges below 4 GB (an app's process has the Java heap, the boot image and
//      more in there; a plain process has nothing);
//   2. reserves the largest for the engine's heap, which src/native/native_lowmem.c maps when the library first
//      allocates (BO1_LOWMEM_BASE / _MB / _RESERVED), and IMAGE_RESERVE bytes for the library, in another range
//      when one is big enough;
//   3. android_dlopen_ext(libbo1.so) into the reservation and calls bo1_android_main (src/web/web_main.cpp).
//
// In the app the game files are expected in the app's external files directory (SDL_AndroidGetExternalStoragePath,
// /sdcard/Android/data/<package>/files): game/ is the install (the folder that has localization.txt and zone/),
// home/ is created for what the engine writes, bo1.log receives stdout and stderr, and args.txt, if present, holds
// extra engine arguments (one line, e.g. "+set bo1_zombies 1 +devmap zombie_theater").
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define IMAGE_RESERVE ((uintptr_t)256 << 20)   // libbo1.so: ~16 MB of code, ~150 MB of zero-initialized data
#define HEAP_MINIMUM ((uintptr_t)1024 << 20)   // zombie_theater touches about 1.5 GB in game
#define LOW_FLOOR ((uintptr_t)64 << 20)        // nothing below 64 MB (null-pointer offsets must keep faulting)
#define LOW_CEILING ((uintptr_t)1 << 32)

static void Say(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, "bo1", "%s", line);
    fprintf(stderr, "bo1_boot: %s\n", line);
    fflush(stderr);
}

struct Range
{
    uintptr_t start, end;
};

// The unmapped ranges inside [LOW_FLOOR, LOW_CEILING), each with a margin to its neighbours and 2 MB alignment.
// Returns how many were written, or -1 if /proc/self/maps cannot be read.
static int FindLowRanges(struct Range *out, int capacity)
{
    const uintptr_t align = (uintptr_t)2 << 20;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return -1;
    int count = 0;
    uintptr_t cursor = LOW_FLOOR;
    char line[512];
    int more = 1;
    while (more)
    {
        unsigned long long start = LOW_CEILING, end = LOW_CEILING;
        if (!fgets(line, sizeof(line), maps))
            more = 0;
        else if (sscanf(line, "%llx-%llx", &start, &end) != 2)
            continue;
        if (start >= LOW_CEILING)
        {
            start = end = LOW_CEILING;
            more = 0;
        }
        if (start > cursor)
        {
            const uintptr_t from = (cursor + align + align - 1) & ~(align - 1);
            const uintptr_t to = ((uintptr_t)start - align) & ~(align - 1);
            if (to > from && to - from >= ((uintptr_t)16 << 20) && count < capacity)
            {
                out[count].start = from;
                out[count].end = to;
                ++count;
            }
        }
        if (end > cursor)
            cursor = (uintptr_t)end;
    }
    fclose(maps);
    return count;
}

static int Reserve(uintptr_t start, uintptr_t size)
{
    void *reserved = mmap((void *)start, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
        -1, 0);
    if (reserved == MAP_FAILED || (uintptr_t)reserved != start)
    {
        // (a kernel without MAP_FIXED_NOREPLACE takes the address as a hint and may answer with another one)
        Say("could not reserve %p + %lu MB (%s)", (void *)start, (unsigned long)(size >> 20),
            reserved == MAP_FAILED ? strerror(errno) : "moved");
        if (reserved != MAP_FAILED)
            munmap(reserved, size);
        return 0;
    }
    return 1;
}

// Loads the engine library into low memory and runs it. Returns the engine's exit code, or a negative number.
static int BootRun(const char *libraryPath, int argc, char **argv)
{
    struct Range ranges[64];
    const int count = FindLowRanges(ranges, 64);
    if (count < 0)
    {
        Say("cannot read /proc/self/maps (%s)", strerror(errno));
        return -1;
    }
    // The heap gets the largest range. The library goes into the smallest other range that holds it, so that it
    // does not take from the heap; without one, into the start of the heap's range.
    int heap = -1, image = -1;
    for (int i = 0; i < count; ++i)
    {
        Say("free below 4 GB: %p..%p (%lu MB)", (void *)ranges[i].start, (void *)ranges[i].end,
            (unsigned long)((ranges[i].end - ranges[i].start) >> 20));
        if (heap < 0 || ranges[i].end - ranges[i].start > ranges[heap].end - ranges[heap].start)
            heap = i;
    }
    // The library below 2 GB when a range there holds it: the engine's own addresses (functions, globals) are
    // then positive as 32-bit integers, which is all the Linux builds ever ran with. Placed above 2 GB, the first
    // level load crashed on a sign-extended address of a global (Material_IsDefault from UI_SetLoadingScreenMaterial).
    for (int pass = 0; pass < 2 && image < 0; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            const uintptr_t size = ranges[i].end - ranges[i].start;
            const int below2G = ranges[i].start + IMAGE_RESERVE <= ((uintptr_t)1 << 31);
            if (i != heap && size >= IMAGE_RESERVE && (pass == 1 || below2G)
                && (image < 0 || size < ranges[image].end - ranges[image].start))
                image = i;
        }
    }
    if (heap < 0)
    {
        Say("nothing is free below 4 GB");
        return -2;
    }
    uintptr_t imageBase, heapBase = ranges[heap].start;
    const uintptr_t heapEnd = ranges[heap].end;
    if (image >= 0)
        imageBase = ranges[image].start;
    else
    {
        imageBase = heapBase;
        heapBase += IMAGE_RESERVE;
    }
    if (imageBase + IMAGE_RESERVE > ((uintptr_t)1 << 31))
        Say("warning: no room for the library below 2 GB; it loads at %p", (void *)imageBase);
    if (heapEnd <= heapBase || heapEnd - heapBase < HEAP_MINIMUM)
    {
        Say("not enough room below 4 GB: %lu MB for the heap (%lu MB needed)",
            (unsigned long)((heapEnd > heapBase ? heapEnd - heapBase : 0) >> 20), (unsigned long)(HEAP_MINIMUM >> 20));
        return -2;
    }
    const uintptr_t heapSize = heapEnd - heapBase;
    if (!Reserve(imageBase, IMAGE_RESERVE) || !Reserve(heapBase, heapSize))
        return -3;
    char number[32];
    snprintf(number, sizeof(number), "0x%lx", (unsigned long)heapBase);
    setenv("BO1_LOWMEM_BASE", number, 1);
    snprintf(number, sizeof(number), "%lu", (unsigned long)(heapSize >> 20));
    setenv("BO1_LOWMEM_MB", number, 1);
    setenv("BO1_LOWMEM_RESERVED", "1", 1);
    Say("low memory: library at %p (%lu MB reserved), heap at %p (%lu MB), page size %ld", (void *)imageBase,
        (unsigned long)(IMAGE_RESERVE >> 20), (void *)heapBase, (unsigned long)(heapSize >> 20), sysconf(_SC_PAGESIZE));
    const uintptr_t start = imageBase;

    android_dlextinfo info;
    memset(&info, 0, sizeof(info));
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = (void *)start;
    info.reserved_size = IMAGE_RESERVE;
    void *library = android_dlopen_ext(libraryPath, RTLD_NOW | RTLD_LOCAL, &info);
    if (!library)
    {
        Say("could not load %s: %s", libraryPath, dlerror());
        return -4;
    }
    int (*entry)(int, char **) = (int (*)(int, char **))dlsym(library, "bo1_android_main");
    if (!entry)
    {
        Say("%s has no bo1_android_main", libraryPath);
        return -5;
    }
    if ((uintptr_t)entry >= LOW_CEILING)
    {
        Say("%s was loaded at %p, not in the reserved range", libraryPath, (void *)(uintptr_t)entry);
        return -6;
    }
    Say("engine entry at %p", (void *)(uintptr_t)entry);
    return entry(argc, argv);
}

#ifdef BO1_BOOT_TEST

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: bo1_boot_test <libbo1.so> [engine arguments]\n");
        return 2;
    }
    if (getenv("BO1_BOOT_TEST_APP_LAYOUT"))
    {
        // what an app's process has below 4 GB, roughly: the Java heap's reservation and the boot image
        mmap((void *)0x12c00000, (size_t)0x40000000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
        mmap((void *)0x6f000000, (size_t)0x08000000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    }
    return BootRun(argv[1], argc - 1, argv + 1);
}

#else

#include <SDL2/SDL.h>

void Bo1SelectVulkanDriver(const char *files, const char *nativeDir);   // bo1_driver.c

// splits a line of arguments at spaces into argv (no quoting: engine arguments do not need it)
static int SplitArguments(char *text, char **argv, int argc, int capacity)
{
    for (char *token = strtok(text, " \t\r\n"); token && argc < capacity - 1; token = strtok(NULL, " \t\r\n"))
        argv[argc++] = token;
    return argc;
}

int SDL_main(int argc, char **argv)
{
    static char game[1024], home[1024], logPath[1024], argsPath[1024], library[1024], extra[4096];
    const char *files = SDL_AndroidGetExternalStoragePath();
    if (!files)
    {
        Say("no external files directory: %s", SDL_GetError());
        return 1;
    }
    snprintf(game, sizeof(game), "%s/game", files);
    snprintf(home, sizeof(home), "%s/home", files);
    snprintf(logPath, sizeof(logPath), "%s/bo1.log", files);
    snprintf(argsPath, sizeof(argsPath), "%s/args.txt", files);
    mkdir(game, 0775);
    mkdir(home, 0775);

    // stdout and stderr have no terminal in an app: the log file takes both
    // the log of the start before this one stays as bo1.prev.log (a crash is usually read after the next start)
    {
        static char previous[1100];
        snprintf(previous, sizeof(previous), "%s/bo1.prev.log", files);
        rename(logPath, previous);
    }
    const int log = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (log >= 0)
    {
        dup2(log, 1);
        dup2(log, 2);
        close(log);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }

    // libbo1.so is next to this library (the app's native library directory)
    Dl_info self;
    if (!dladdr((void *)(uintptr_t)SDL_main, &self) || !self.dli_fname)
    {
        Say("cannot find the native library directory");
        return 1;
    }
    snprintf(library, sizeof(library), "%s", self.dli_fname);
    char *slash = strrchr(library, '/');
    snprintf(slash ? slash + 1 : library, sizeof(library) - (size_t)(slash ? slash + 1 - library : 0), "libbo1.so");

    // mod (turnip): the Vulkan driver, the system's or the one files/driver.txt names (bo1_driver.c)
    if (slash)
    {
        *slash = 0;
        Bo1SelectVulkanDriver(files, library);
        *slash = '/';
    }

    char marker[1100];
    snprintf(marker, sizeof(marker), "%s/localization.txt", game);
    if (access(marker, R_OK) != 0)
    {
        Say("game files not found: copy the Black Ops install into %s (it must contain localization.txt and zone/)", game);
        static char message[1400];
        snprintf(message, sizeof(message), "Game files not found.\n\nCopy the contents of your \"Call of Duty Black Ops\" "
            "folder (localization.txt, zone, main, ...) into\n\n%s\n\non the device, then start again.", game);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Black Ops", message, NULL);
        return 1;
    }
    if (chdir(game) != 0)
        Say("chdir(%s): %s", game, strerror(errno));

    char *engineArgv[96];
    int engineArgc = 0;
    engineArgv[engineArgc++] = (char *)"bo1";
    engineArgv[engineArgc++] = (char *)"+set";
    engineArgv[engineArgc++] = (char *)"fs_b";
    engineArgv[engineArgc++] = game;
    engineArgv[engineArgc++] = (char *)"+set";
    engineArgv[engineArgc++] = (char *)"fs_h";
    engineArgv[engineArgc++] = home;
    // the back buffer is the engine's mode (720p unless r_mode says otherwise), scaled to the display
    engineArgv[engineArgc++] = (char *)"+set";
    engineArgv[engineArgc++] = (char *)"r_fullscreen";
    engineArgv[engineArgc++] = (char *)"0";
    // (from the activity: --refresh=<the display's highest refresh rate>, Bo1Activity.getArguments)
    int refresh = 0;
    for (int i = 1; i < argc && engineArgc < 95; ++i)
    {
        if (!strncmp(argv[i], "--refresh=", 10))
            refresh = atoi(argv[i] + 10);
        else if (!strncmp(argv[i], "--rates=", 8))
            setenv("BO1_REFRESH_RATES", argv[i] + 8, 1);   // the engine's list of refresh rates (d3d9_direct3d.cpp)
        else
            engineArgv[engineArgc++] = argv[i];
    }
    // args.txt holds the rest. Without the file the app starts the game's own front end (title screen, then the
    // main menu with Campaign / Multiplayer / Zombies), which a controller's d-pad and A drive.
    // next_args.txt: the arguments of one start, written by the engine when it starts the app again in its
    // other mode or for a map (src/native/native_extras.cpp). Used once, in place of args.txt.
    static char nextPath[1100];
    snprintf(nextPath, sizeof(nextPath), "%s/next_args.txt", files);
    {
        // TEST SWITCH: watch.txt holds an address for the write watch of native_x86_unaligned.cpp
        static char watchPath[1100];
        snprintf(watchPath, sizeof(watchPath), "%s/watch.txt", files);
        setenv("BO1_WATCH_FILE", watchPath, 1);
        // TEST SWITCH: cmd.txt holds console commands for the running game (native_extras.cpp)
        static char cmdPath[1100];
        snprintf(cmdPath, sizeof(cmdPath), "%s/cmd.txt", files);
        remove(cmdPath);
        setenv("BO1_CMD_FILE", cmdPath, 1);
    }
    FILE *argsFile = fopen(nextPath, "r");
    if (argsFile)
        unlink(nextPath);
    else
        argsFile = fopen(argsPath, "r");
    if (argsFile)
    {
        const size_t count = fread(extra, 1, sizeof(extra) - 1, argsFile);
        fclose(argsFile);
        extra[count] = 0;
    }
    else
        snprintf(extra, sizeof(extra), "+set bo1_zombies 1");
    engineArgc = SplitArguments(extra, engineArgv, engineArgc, 96);
    engineArgv[engineArgc] = NULL;
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   // back is Escape for the engine, not "leave the app"
    // The display, for the engine's list of video modes (native_video.cpp reads these): its size, landscape, and
    // the mode a first start uses: the display's shape at 720 lines (a 2340x1080 phone: 1560x720), so that the
    // picture fills the screen. The video mode row of the graphics menu changes it.
    {
        SDL_DisplayMode mode;
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0 && SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
        {
            const int w = mode.w > mode.h ? mode.w : mode.h, h = mode.w > mode.h ? mode.h : mode.w;
            const int lines = h < 720 ? h : 720;
            char text[32];
            snprintf(text, sizeof(text), "%dx%d", w, h);
            setenv("BO1_DISPLAY_SIZE", text, 1);
            snprintf(text, sizeof(text), "%dx%d", (w * lines / h) & ~1, lines);
            setenv("BO1_DEFAULT_MODE", text, 0);
            Say("display %dx%d, default mode %s", w, h, text);
            // On a first start (no configuration saved yet) the mode is also given on the command line: the
            // game's stock configure_mp.cfg sets r_mode 1024x768, which the command line overrides. Later starts
            // use what the configuration saved.
            static char firstMode[32], configPath[1100];
            struct stat info;
            // (Zombies keeps its configuration in config.cfg, Multiplayer in config_mp.cfg)
            int zombies = 0, sharedConfig = 0;
            const char *modDir = NULL;
            for (int i = 0; i + 2 < engineArgc; ++i)
            {
                zombies |= !strcmp(engineArgv[i + 1], "bo1_zombies") && strcmp(engineArgv[i + 2], "0");
                sharedConfig |= !strcmp(engineArgv[i + 1], "bo1_mod_sharedconfig") && strcmp(engineArgv[i + 2], "0");
                if (!strcmp(engineArgv[i + 1], "fs_game"))
                    modDir = engineArgv[i + 2];
            }
            // mod (custom maps): with a mod the configuration is a per-mod copy (players/mods/<x>/, unless
            // bo1_mod_sharedconfig); the mod comes from the command line or the Extras screen's bo1_mod.txt
            static char modFromFile[128];
            if (!modDir)
            {
                char path[1100];
                snprintf(path, sizeof(path), "%s/bo1_mod.txt", home);
                FILE *f = fopen(path, "r");
                if (f)
                {
                    char name[64] = "";
                    if (fgets(name, sizeof(name), f))
                    {
                        name[strcspn(name, "\r\n")] = 0;
                        if (name[0])
                        {
                            snprintf(modFromFile, sizeof(modFromFile), "mods/%s", name);
                            modDir = modFromFile;
                        }
                    }
                    fclose(f);
                }
            }
            if (modDir && modDir[0] && !sharedConfig)
                snprintf(configPath, sizeof(configPath), "%s/players/%s/%s", home, modDir,
                         zombies ? "config.cfg" : "config_mp.cfg");
            else
                snprintf(configPath, sizeof(configPath), "%s/players/%s", home, zombies ? "config.cfg" : "config_mp.cfg");
            if (stat(configPath, &info) != 0 && engineArgc + 3 < 96)
            {
                snprintf(firstMode, sizeof(firstMode), "%s", getenv("BO1_DEFAULT_MODE"));
                engineArgv[engineArgc++] = (char *)"+set";
                engineArgv[engineArgc++] = (char *)"r_mode";
                engineArgv[engineArgc++] = firstMode;
                // ... and the frame rate limit is the display's refresh rate (the engine's default is 85)
                static char firstFps[16];
                if (refresh >= 50 && engineArgc + 3 < 96)
                {
                    snprintf(firstFps, sizeof(firstFps), "%d", refresh);
                    engineArgv[engineArgc++] = (char *)"+set";
                    engineArgv[engineArgc++] = (char *)"com_maxfps";
                    engineArgv[engineArgc++] = firstFps;
                }
                // ... and no multisampling (the PC default is 4 samples, which costs a phone's GPU a lot at these
                // pixel densities); the graphics menu's anti-aliasing row turns it on
                if (engineArgc + 3 < 96)
                {
                    engineArgv[engineArgc++] = (char *)"+set";
                    engineArgv[engineArgc++] = (char *)"r_aaSamples";
                    engineArgv[engineArgc++] = (char *)"1";
                }
                engineArgv[engineArgc] = NULL;
            }
        }
    }

    const int result = BootRun(library, engineArgc, engineArgv);
    Say("engine returned %d", result);
    if (result < 0)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Black Ops",
            "The engine could not be loaded below 4 GB on this device. See bo1.log in the app's files folder.", NULL);
    return result;
}

#endif
