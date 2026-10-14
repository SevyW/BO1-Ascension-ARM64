// native_extras.cpp - what the engine-drawn screens of the native builds (client_mp/cl_extras.cpp, cl_play.cpp,
// cl_joinmenu.cpp) need from the host: files and folders by plain path, this device's address, sending the log,
// and starting the game again with other arguments.
//
// Restart: Zombies and Multiplayer are two modes of one program, chosen on the command line (bo1_zombies), like the
// two retail executables. Going from one to the other is a new start:
//   Linux    execv of the program itself with +set fs_b / fs_h and the new arguments.
//   Android  the arguments go to <files>/next_args.txt, which the loader (android/bo1_boot.c) uses once instead of
//            args.txt, and the activity is started again (Bo1Activity.restartApp).
// bo1_native_restart_at_exit only notes it; the engine then quits the usual way (configuration written) and
// bo1_web_exit (web/compat/win_crt.cpp) calls bo1_native_restart_now in place of ending the process.
//
// All functions take and return plain C types with no pointer-to-pointer (the callers are built with 4-byte
// pointers, tools/p32). This file is not rewritten.
#include <dirent.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <jni.h>
#endif

namespace
{
std::string s_restartBase, s_restartHome, s_restartArgs;
bool s_restartPending;

#ifdef __ANDROID__
// a method of the activity (android/java/org/bo1port/zombies/Bo1Activity.java), with an optional string argument
void CallActivity(const char *method, const char *text)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity)
        return;
    jclass type = env->GetObjectClass(activity);
    if (text)
    {
        jmethodID id = env->GetMethodID(type, method, "(Ljava/lang/String;)V");
        jstring value = env->NewStringUTF(text);
        if (id)
            env->CallVoidMethod(activity, id, value);
        env->DeleteLocalRef(value);
    }
    else
    {
        jmethodID id = env->GetMethodID(type, method, "()V");
        if (id)
            env->CallVoidMethod(activity, id);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
#endif

// "a b \"c d\"" -> a, b, c d
std::vector<std::string> Split(const std::string &text)
{
    std::vector<std::string> out;
    std::string word;
    bool quoted = false, any = false;
    for (char c : text)
    {
        if (c == '"')
        {
            quoted = !quoted;
            any = true;
        }
        else if ((c == ' ' || c == '\n' || c == '\t') && !quoted)
        {
            if (any)
                out.push_back(word);
            word.clear();
            any = false;
        }
        else
        {
            word += c;
            any = true;
        }
    }
    if (any)
        out.push_back(word);
    return out;
}
}

extern "C" int bo1_native_file_exists(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0;
}

// The index-th entry of a folder that is a folder (wantFolders) or a file, in the folder's own order; 0 past the
// end. Names starting with '.' are skipped.
extern "C" int bo1_native_list_dir(const char *path, int index, int wantFolders, char *name, int size)
{
    DIR *dir = path ? opendir(path) : nullptr;
    if (!dir)
        return 0;
    int found = 0;
    while (dirent *entry = readdir(dir))
    {
        if (entry->d_name[0] == '.')
            continue;
        std::string full = std::string(path) + "/" + entry->d_name;
        struct stat info;
        if (stat(full.c_str(), &info) != 0 || (S_ISDIR(info.st_mode) != 0) != (wantFolders != 0))
            continue;
        if (index-- == 0)
        {
            snprintf(name, (size_t)size, "%s", entry->d_name);
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

// the first line of a small text file, without the line end; 0 when there is no file
extern "C" int bo1_native_read_line(const char *path, char *text, int size)
{
    text[0] = 0;
    FILE *file = path ? fopen(path, "r") : nullptr;
    if (!file)
        return 0;
    if (!fgets(text, size, file))
        text[0] = 0;
    fclose(file);
    text[strcspn(text, "\r\n")] = 0;
    return 1;
}

// writes the line; an empty one removes the file
extern "C" int bo1_native_write_line(const char *path, const char *text)
{
    if (!path)
        return 0;
    if (!text || !text[0])
        return unlink(path) == 0;
    FILE *file = fopen(path, "w");
    if (!file)
        return 0;
    fprintf(file, "%s\n", text);
    fclose(file);
    return 1;
}

// this device's address on the local network (the first IPv4 address that is up and not loopback); "" when none
extern "C" void bo1_native_local_address(char *text, int size)
{
    text[0] = 0;
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0)
        return;
    for (ifaddrs *i = list; i; i = i->ifa_next)
    {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
            continue;
        inet_ntop(AF_INET, &((sockaddr_in *)i->ifa_addr)->sin_addr, text, (socklen_t)size);
        break;
    }
    freeifaddrs(list);
}

// Android: the end of <homePath>/../bo1.log through the system's share sheet (mail, messengers, ...). Elsewhere the
// log is the program's output; this only says so.
extern "C" void bo1_native_send_log(const char *homePath)
{
#ifdef __ANDROID__
    std::string path = std::string(homePath ? homePath : "") + "/../bo1.log";
    fflush(stdout);
    fflush(stderr);
    CallActivity("shareLog", path.c_str());
#else
    (void)homePath;
    printf("native: send log: the log is this program's output (stdout / stderr)\n");
#endif
}

// The display's refresh rate (the engine's r_displayRefresh). Android: the activity asks for the display mode with
// that rate; elsewhere the window system's rate stays.
extern "C" void bo1_native_set_refresh_rate(int hz)
{
    static int s_last = 0;
    if (hz == s_last)
        return;
    s_last = hz;
    printf("native: refresh rate %d Hz asked for\n", hz);
#ifdef __ANDROID__
    char text[16];
    snprintf(text, sizeof(text), "%d", hz);
    CallActivity("setRefreshRate", text);
#endif
}

extern "C" void bo1_native_restart_at_exit(const char *basePath, const char *homePath, const char *args)
{
    s_restartBase = basePath ? basePath : "";
    s_restartHome = homePath ? homePath : "";
    s_restartArgs = args ? args : "";
    s_restartPending = true;
}

extern "C" int bo1_native_restart_pending(void)
{
    return s_restartPending;
}

// does not return when a restart was asked for and could be started
extern "C" void bo1_native_restart_now(void)
{
    if (!s_restartPending)
        return;
    s_restartPending = false;
    printf("native: starting again with: %s\n", s_restartArgs.c_str());
    fflush(stdout);
    fflush(stderr);
#ifdef __ANDROID__
    const std::string file = s_restartHome + "/../next_args.txt";
    if (FILE *out = fopen(file.c_str(), "w"))
    {
        fprintf(out, "%s\n", s_restartArgs.c_str());
        fclose(out);
    }
    CallActivity("restartApp", nullptr);   // starts the activity again and ends this process
    sleep(3);
#else
    char self[1024];
    const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0)
        return;
    self[length] = 0;
    std::vector<std::string> words = { self, "+set", "fs_b", s_restartBase, "+set", "fs_h", s_restartHome };
    for (const std::string &word : Split(s_restartArgs))
        words.push_back(word);
    std::vector<char *> argv;
    for (std::string &word : words)
        argv.push_back(&word[0]);
    argv.push_back(nullptr);
    execv(self, argv.data());
    perror("native: execv");
#endif
}

// TEST SWITCH: BO1_CMD_FILE names a file (Android: <files>/cmd.txt, set by bo1_boot.c). About once a second the main
// thread looks for it; its text goes to the console's command buffer and the file is removed. For driving a running
// game from adb: adb shell "echo 'openmenu main' > .../files/cmd.txt".
void Cbuf_AddText(int localClientNum, const char *text);
extern "C" void bo1_native_poll_commands(void)
{
    static const char *path = getenv("BO1_CMD_FILE");
    static unsigned frame;
    if (!path || !*path || (++frame & 63))
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    static char text[4096];
    const size_t n = fread(text, 1, sizeof(text) - 2, f);
    fclose(f);
    remove(path);
    text[n] = '\n';
    text[n + 1] = 0;
    Cbuf_AddText(0, text);
}
