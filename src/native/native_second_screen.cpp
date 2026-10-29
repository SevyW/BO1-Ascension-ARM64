// native_second_screen.cpp - mod (native): the second-screen controller of dual-screen Android devices (AYN Thor).
//
// The trackpads and grip buttons are drawn and read in Java (android/java/org/bo1port/zombies/SecondScreen*.java),
// on a Presentation on the device's second display. Every touch event reaches this file through a JNI call on the
// UI thread; what it carries is kept here and handed to the engine once per frame on the window's thread
// (bo1_native_gamepad_update, the one producer of the input queue):
//   - the right pad's look movement: mouse movement, the path a real mouse takes in a match (pixels of a 1280x720
//     picture, kept in 1/256 so slow movement is not lost);
//   - the left pad as a joystick: the pad's left stick (slot 0, as the touch controls do);
//   - grips, pad clicks, d-pad zones, scroll steps and radial slices: presses of keys nothing else uses on Android
//     (AUX1..AUX12, KP_*), which are bound like any key ("bind AUX1 +gostand", saved in players/config.cfg);
//   - console commands from the remapping screen (bind, unbind), and on request the binds of a list of keys, sent
//     back to the activity (secondScreenBinds).
// libbo1.so is not loaded by the Java class loader, so Java cannot find these methods by name: they are registered
// on the activity's class from the window's thread, and the activity learns that they work from the first
// secondScreenState call. Log lines start with "second screen:".
#include <client/gpad_backend.h>
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <SDL2/SDL_system.h>
#include <jni.h>
#endif

// the engine's (C++ linkage)
void Cbuf_AddText(int localClientNum, const char *text);
const char *Dvar_GetVariantString(const char *dvarName);
int Key_StringToKeynum(const char *str);
const char *Key_GetBinding(int localClientNum, unsigned int keynum, int bindNum);

extern "C" {
// src/web/web_input.cpp
void bo1_native_input_motion(int dx, int dy, int x, int y, int width, int height);
void bo1_native_input_keycode(int key, int down);
#ifdef __ANDROID__
// native_extras.cpp
void bo1_native_call_activity(const char *method, const char *text);
#endif
}

namespace
{
std::atomic<long long> s_lookX{0}, s_lookY{0};   // 1/256 pixel
std::atomic<int> s_stickX{0}, s_stickY{0};       // 1/32767
std::atomic<int> s_visible{0};                   // the pads are on the second screen
std::atomic<int> s_enabled{-1}, s_ingame{-1};    // from the engine (cl_touch.cpp); -1 not known yet

std::mutex s_lock;   // the JNI thread and the window's thread
std::vector<int> s_keys;   // key << 1 | down
std::vector<std::string> s_commands;
std::string s_bindsWanted;   // key names, space separated ("" nothing asked)

bool s_registered;
int s_registerTries;
std::string s_lastState;

void QueueKey(int key, bool down)
{
    if (key <= 0 || key >= 256)
        return;
    std::lock_guard<std::mutex> hold(s_lock);
    s_keys.push_back(key << 1 | (down ? 1 : 0));
}

#ifdef __ANDROID__
void JNICALL Look(JNIEnv *, jclass, jfloat dx, jfloat dy)
{
    s_lookX.fetch_add((long long)(dx * 256.0f));
    s_lookY.fetch_add((long long)(dy * 256.0f));
}

void JNICALL Stick(JNIEnv *, jclass, jfloat x, jfloat y)
{
    const float cx = x < -1 ? -1 : x > 1 ? 1 : x, cy = y < -1 ? -1 : y > 1 ? 1 : y;
    s_stickX.store((int)(cx * 32767.0f));
    s_stickY.store((int)(cy * 32767.0f));
}

void JNICALL Key(JNIEnv *, jclass, jint key, jboolean down)
{
    QueueKey(key, down != 0);
}

std::string Utf(JNIEnv *env, jstring text)
{
    if (!text)
        return std::string();
    const char *chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(text, chars);
    return out;
}

void JNICALL Command(JNIEnv *env, jclass, jstring text)
{
    std::string command = Utf(env, text);
    if (command.empty() || command.find_first_of("\n\r;") != std::string::npos)
        return;   // one command per call
    std::lock_guard<std::mutex> hold(s_lock);
    s_commands.push_back(command);
}

void JNICALL RequestBinds(JNIEnv *env, jclass, jstring keys)
{
    std::string names = Utf(env, keys);
    std::lock_guard<std::mutex> hold(s_lock);
    s_bindsWanted = names.empty() ? " " : names;
}

void JNICALL Visible(JNIEnv *, jclass, jboolean visible)
{
    s_visible.store(visible ? 1 : 0);
    if (!visible)
    {
        s_stickX.store(0);
        s_stickY.store(0);
    }
}

void JNICALL Log(JNIEnv *env, jclass, jstring text)
{
    printf("second screen: %s\n", Utf(env, text).c_str());
    fflush(stdout);
}

// the methods of Bo1Activity (static native) this file provides
void Register()
{
    if (s_registered || s_registerTries >= 3)
        return;
    ++s_registerTries;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity)
        return;
    static const JNINativeMethod kMethods[] = {
        {"ssLook", "(FF)V", (void *)Look},
        {"ssStick", "(FF)V", (void *)Stick},
        {"ssKey", "(IZ)V", (void *)Key},
        {"ssCommand", "(Ljava/lang/String;)V", (void *)Command},
        {"ssRequestBinds", "(Ljava/lang/String;)V", (void *)RequestBinds},
        {"ssVisible", "(Z)V", (void *)Visible},
        {"ssLog", "(Ljava/lang/String;)V", (void *)Log},
    };
    jclass type = env->GetObjectClass(activity);
    const jint result = env->RegisterNatives(type, kMethods, (jint)(sizeof(kMethods) / sizeof(kMethods[0])));
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (result == JNI_OK)
    {
        s_registered = true;
        printf("second screen: natives registered\n");
    }
    else
    {
        printf("second screen: RegisterNatives failed (%d)\n", (int)result);
        s_registerTries = 3;
    }
}
#endif

// "KEY=binding\n" for each key name in the list (unknown names skipped)
std::string Binds(const std::string &names)
{
    std::string out;
    size_t at = 0;
    while (at < names.size())
    {
        const size_t end = names.find(' ', at);
        const std::string name = names.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? names.size() : end + 1;
        if (name.empty())
            continue;
        const int key = Key_StringToKeynum(name.c_str());
        if (key < 0 || key >= 256)
            continue;
        const char *binding = Key_GetBinding(0, (unsigned int)key, 0);
        out += name;
        out += '=';
        out += binding ? binding : "";
        out += '\n';
    }
    return out;
}
}

// cl_touch.cpp, every frame: bo1_second_screen, and whether a game is being played with no menu open
extern "C" void bo1_native_second_screen_settings(int enabled, int ingame)
{
    s_enabled.store(enabled ? 1 : 0);
    s_ingame.store(ingame ? 1 : 0);
}

// native_gamepad.cpp, every frame on the window's thread, with the state of pad slot 0 about to be published
extern "C" void bo1_native_second_screen_frame(GPadRawState *slot0)
{
#ifdef __ANDROID__
    Register();
    if (!s_registered)
        return;
#endif
    std::vector<int> keys;
    std::vector<std::string> commands;
    std::string wanted;
    {
        std::lock_guard<std::mutex> hold(s_lock);
        keys.swap(s_keys);
        commands.swap(s_commands);
        wanted.swap(s_bindsWanted);
    }
    for (const std::string &command : commands)
    {
        printf("second screen: %s\n", command.c_str());
        Cbuf_AddText(0, command.c_str());
        Cbuf_AddText(0, "\n");
    }
    for (int k : keys)
        bo1_native_input_keycode(k >> 1, k & 1);

    // whole pixels of look; the rest stays for the next frame
    const long long lx = s_lookX.load(), ly = s_lookY.load();
    const int dx = (int)(lx / 256), dy = (int)(ly / 256);
    if (dx || dy)
    {
        s_lookX.fetch_sub((long long)dx * 256);
        s_lookY.fetch_sub((long long)dy * 256);
        bo1_native_input_motion(dx, dy, 0, 0, 0, 0);   // no size: the menus' pointer stays where it is
    }

    if (slot0 && s_visible.load())
    {
        slot0->connected = true;
        const int sx = s_stickX.load(), sy = s_stickY.load();
        if (sx || sy)
        {
            slot0->lx = (float)sx / 32767.0f;
            slot0->ly = (float)sy / 32767.0f;
        }
    }

#ifdef __ANDROID__
    if (!wanted.empty())
        bo1_native_call_activity("secondScreenBinds", Binds(wanted).c_str());
    // what the activity shows and does depends on: the EXTRAS switch, a game or a menu, Zombies or Multiplayer
    const char *zombies = Dvar_GetVariantString("bo1_zombies");
    char state[64];
    snprintf(state, sizeof(state), "%d %d %d", s_enabled.load(), s_ingame.load(), !zombies || !*zombies ? -1 : atoi(zombies) ? 1 : 0);
    if (s_lastState != state)
    {
        s_lastState = state;
        bo1_native_call_activity("secondScreenState", state);
    }
#else
    (void)wanted;
#endif
}
