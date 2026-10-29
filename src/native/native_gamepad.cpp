// native_gamepad.cpp - game controllers for the native build, through SDL2's game controller API.
//
// The engine's pad code (client/gpad_core.cpp) reads the backend of client/gpad_backend.h, which in this build is
// src/web/web_gamepad.cpp: it reads the bo1_gamepad array that the browser page fills. Here SDL fills the same array
// (XInput button bits, triggers 0..1, sticks -1..1 with Y up) and plays the rumble values the engine writes back.
// bo1_native_gamepad_update runs once per frame on the window's thread (native_video.cpp), where SDL's events are
// pumped.
//
// Test pad: BO1_TESTPAD="<seconds>:<control>[:<milliseconds>],..." scripts slot 0 without a controller, for
// unattended runs on a machine that has none. Controls: a b x y start back up down left right lb rb ls rs (buttons),
// lt rt (triggers, full), lx+ lx- ly+ ly- rx+ rx- ry+ ry- (a stick pushed fully one way). Default hold 150 ms.
// click@<x>@<y>: not the pad but a left mouse click at that point of a 1280x720 window (the PC menus are mouse menus).
// cmd@<text>: a console command. mr@<menu index>@<response>: what a script menu sends when an entry is chosen
// ("mr <server id> <menu> <response>"; team menu: autoassign, class menu: a class name).
// finger@<x>@<y>[@<x2>@<y2>]: a finger on the touch controls (native_touch.cpp) at that point of a 1280x720
// picture for the hold time, moving to the second point when one is given. With a finger step in the script the
// pad is the touch controls' (set bo1_touch 1).
// Example: BO1_TESTPAD="20:a,24:down,26:a,60:ly+:3000". When set, real controllers are ignored.
#include "native_video.h"
#include "native_touch.h"

#include <SDL2/SDL.h>
#include <client/gpad_backend.h>
#include <web_bridge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// the engine's (C++ linkage)
void Cbuf_AddText(int localClientNum, const char *text);
const char *Dvar_GetVariantString(const char *dvarName);

namespace
{
SDL_GameController *s_controllers[BO1_MAX_GAMEPADS];
bool s_init;
float s_rumbleLow[BO1_MAX_GAMEPADS], s_rumbleHigh[BO1_MAX_GAMEPADS];

struct TestStep
{
    uint32_t startMs, endMs;
    unsigned buttons;
    float lt, rt, lx, ly, rx, ry;
    bool click;        // a left mouse click at clickX, clickY (in a 1280x720 window) instead of a pad control
    int clickX, clickY;
    int clickState;    // 0 not yet, 1 held, 2 released
    std::string command;   // a console command instead of a pad control ("cmd@...", "mr@...")
    bool finger;       // a finger on the touch controls, from (fingerX, fingerY) to (fingerX2, fingerY2), 0..1
    float fingerX, fingerY, fingerX2, fingerY2;
    int fingerState;   // 0 not yet, 1 down, 2 up
    bool menuResponse;
    bool done;
};

extern "C" {
// src/web/web_input.cpp
void bo1_native_input_motion(int dx, int dy, int x, int y, int width, int height);
void bo1_native_input_buttons(int mask);
void bo1_native_input_focus(int active);
// native_second_screen.cpp
void bo1_native_second_screen_frame(GPadRawState *slot0);
}
std::vector<TestStep> s_test;
std::vector<SDL_JoystickID> s_ignored;   // devices looked at once and found not to be game pads
bool s_testMode;
bool s_testFingers;   // the script has finger steps
uint32_t s_testStart;

void Publish(int slot, const GPadRawState &s)
{
    bo1_gamepad *pads = bo1_input_gamepads();
    bo1_gamepad &p = pads[slot];
    p.connected = s.connected ? 1u : 0u;
    p.buttons = s.buttons;
    p.lt = s.lt;
    p.rt = s.rt;
    p.lx = s.lx;
    p.ly = s.ly;
    p.rx = s.rx;
    p.ry = s.ry;
    __atomic_fetch_add(&p.seq, 1u, __ATOMIC_RELEASE);   // the reader retries a copy that raced with this write
}

float Axis(SDL_GameController *c, SDL_GameControllerAxis axis)
{
    const int v = SDL_GameControllerGetAxis(c, axis);
    return v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f;
}

unsigned Buttons(SDL_GameController *c)
{
    static const struct { SDL_GameControllerButton sdl; unsigned raw; } map[] = {
        {SDL_CONTROLLER_BUTTON_DPAD_UP, GPAD_RAW_DPAD_UP},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, GPAD_RAW_DPAD_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, GPAD_RAW_DPAD_LEFT},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GPAD_RAW_DPAD_RIGHT},
        {SDL_CONTROLLER_BUTTON_START, GPAD_RAW_START},
        {SDL_CONTROLLER_BUTTON_BACK, GPAD_RAW_BACK},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, GPAD_RAW_LEFT_THUMB},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, GPAD_RAW_RIGHT_THUMB},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GPAD_RAW_LEFT_SHOULDER},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GPAD_RAW_RIGHT_SHOULDER},
        {SDL_CONTROLLER_BUTTON_A, GPAD_RAW_A},
        {SDL_CONTROLLER_BUTTON_B, GPAD_RAW_B},
        {SDL_CONTROLLER_BUTTON_X, GPAD_RAW_X},
        {SDL_CONTROLLER_BUTTON_Y, GPAD_RAW_Y},
    };
    unsigned bits = 0;
    for (const auto &m : map)
        if (SDL_GameControllerGetButton(c, m.sdl))
            bits |= m.raw;
    return bits;
}

// opens controllers that appeared into free slots, drops the ones that went away
void RefreshControllers()
{
    for (int slot = 0; slot < BO1_MAX_GAMEPADS; ++slot)
    {
        if (s_controllers[slot] && !SDL_GameControllerGetAttached(s_controllers[slot]))
        {
            SDL_GameControllerClose(s_controllers[slot]);
            s_controllers[slot] = nullptr;
        }
    }
    const int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i)
    {
        if (!SDL_IsGameController(i))
            continue;
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        bool open = false;
        int freeSlot = -1;
        for (int slot = 0; slot < BO1_MAX_GAMEPADS; ++slot)
        {
            if (s_controllers[slot])
            {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_controllers[slot])) == id)
                    open = true;
            }
            else if (freeSlot < 0)
            {
                freeSlot = slot;
            }
        }
        for (SDL_JoystickID ignored : s_ignored)
            if (ignored == id)
                open = true;
        if (open || freeSlot < 0)
            continue;
        SDL_GameController *opened = SDL_GameControllerOpen(i);
        // Android lists keyboards, remotes and sensors as controllers when they have a few mapped buttons (the
        // emulator's "qwerty2" keyboard, fingerprint readers): a game pad has sticks.
        if (opened && SDL_JoystickNumAxes(SDL_GameControllerGetJoystick(opened)) < 2)
        {
            printf("native: ignoring %s (no sticks: not a game pad)\n", SDL_GameControllerName(opened));
            SDL_GameControllerClose(opened);
            s_ignored.push_back(id);
            continue;
        }
        s_controllers[freeSlot] = opened;
        if (s_controllers[freeSlot])
            printf("native: controller %d: %s\n", freeSlot, SDL_GameControllerName(s_controllers[freeSlot]));
    }
}

// The touch controls' buttons, triggers and stick into a pad state, and their look movement to the engine as
// mouse movement (pixels of a 1280x720 picture).
void MergeTouch(GPadRawState &s)
{
    unsigned buttons = 0;
    float lt = 0, rt = 0, lx = 0, ly = 0;
    int lookX = 0, lookY = 0;
    bo1_native_touch_read(&buttons, &lt, &rt, &lx, &ly, &lookX, &lookY);
    s.buttons |= buttons;
    if (lt > s.lt) s.lt = lt;
    if (rt > s.rt) s.rt = rt;
    if (lx != 0.0f || ly != 0.0f)
    {
        s.lx = lx;
        s.ly = ly;
    }
    if (lookX || lookY)
        bo1_native_input_motion(lookX, lookY, 640, 360, 1280, 720);
}

void ParseTestPad(const char *spec)
{
    static const struct { const char *name; unsigned bit; } buttons[] = {
        {"a", GPAD_RAW_A}, {"b", GPAD_RAW_B}, {"x", GPAD_RAW_X}, {"y", GPAD_RAW_Y},
        {"start", GPAD_RAW_START}, {"back", GPAD_RAW_BACK},
        {"up", GPAD_RAW_DPAD_UP}, {"down", GPAD_RAW_DPAD_DOWN}, {"left", GPAD_RAW_DPAD_LEFT}, {"right", GPAD_RAW_DPAD_RIGHT},
        {"lb", GPAD_RAW_LEFT_SHOULDER}, {"rb", GPAD_RAW_RIGHT_SHOULDER},
        {"ls", GPAD_RAW_LEFT_THUMB}, {"rs", GPAD_RAW_RIGHT_THUMB},
    };
    std::string all(spec);
    size_t at = 0;
    while (at < all.size())
    {
        size_t end = all.find(',', at);
        if (end == std::string::npos)
            end = all.size();
        const std::string item = all.substr(at, end - at);
        at = end + 1;
        const size_t c1 = item.find(':');
        if (c1 == std::string::npos)
            continue;
        const size_t c2 = item.find(':', c1 + 1);
        const double seconds = atof(item.substr(0, c1).c_str());
        const std::string control = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        const int holdMs = c2 == std::string::npos ? 150 : atoi(item.substr(c2 + 1).c_str());
        TestStep step{};
        step.startMs = (uint32_t)(seconds * 1000.0);
        step.endMs = step.startMs + (uint32_t)(holdMs > 0 ? holdMs : 150);
        bool known = false;
        for (const auto &b : buttons)
        {
            if (control == b.name)
            {
                step.buttons = b.bit;
                known = true;
            }
        }
        if (control == "lt") { step.lt = 1; known = true; }
        if (control == "rt") { step.rt = 1; known = true; }
        if (control == "lx+") { step.lx = 1; known = true; }
        if (control == "lx-") { step.lx = -1; known = true; }
        if (control == "ly+") { step.ly = 1; known = true; }
        if (control == "ly-") { step.ly = -1; known = true; }
        if (control == "rx+") { step.rx = 1; known = true; }
        if (control == "rx-") { step.rx = -1; known = true; }
        if (control == "ry+") { step.ry = 1; known = true; }
        if (control == "ry-") { step.ry = -1; known = true; }
        if (!control.compare(0, 4, "cmd@"))
        {
            step.command = control.substr(4);
            known = true;
        }
        if (!control.compare(0, 3, "mr@"))
        {
            step.command = control.substr(3);
            for (char &c : step.command)
                if (c == '@')
                    c = ' ';
            step.menuResponse = true;
            known = true;
        }
        if (!control.compare(0, 6, "click@"))
        {
            const size_t sep = control.find('@', 6);
            if (sep != std::string::npos)
            {
                step.click = true;
                step.clickX = atoi(control.substr(6, sep - 6).c_str());
                step.clickY = atoi(control.substr(sep + 1).c_str());
                known = true;
            }
        }
        if (!control.compare(0, 7, "finger@"))
        {
            float v[4] = { 0, 0, -1, -1 };
            const int count = sscanf(control.c_str() + 7, "%f@%f@%f@%f", &v[0], &v[1], &v[2], &v[3]);
            if (count >= 2)
            {
                step.finger = true;
                step.fingerX = v[0] / 1280.0f;
                step.fingerY = v[1] / 720.0f;
                step.fingerX2 = count == 4 ? v[2] / 1280.0f : step.fingerX;
                step.fingerY2 = count == 4 ? v[3] / 720.0f : step.fingerY;
                s_testFingers = true;
                known = true;
            }
        }
        if (known)
            s_test.push_back(step);
        else
            fprintf(stderr, "native: BO1_TESTPAD: unknown control '%s'\n", control.c_str());
    }
    printf("native: BO1_TESTPAD: %d scripted steps on controller 0 (real controllers ignored)\n", (int)s_test.size());
}

void UpdateTestPad()
{
    const uint32_t now = SDL_GetTicks() - s_testStart;
    GPadRawState s{};
    s.connected = true;
    for (TestStep &step : s_test)
    {
        if (!step.command.empty())
        {
            if (now >= step.startMs && !step.done)
            {
                step.done = true;
                std::string text = step.menuResponse
                    ? std::string("cmd mr ") + Dvar_GetVariantString("sv_serverid") + " " + step.command
                    : step.command;
                fprintf(stderr, "native: BO1_TESTPAD: %s\n", text.c_str());
                text += "\n";
                Cbuf_AddText(0, text.c_str());
            }
            continue;
        }
        if (step.finger)
        {
            const long long id = 1000 + (long long)(&step - &s_test[0]);
            if (now >= step.startMs && step.fingerState == 0)
            {
                bo1_native_touch_finger(0, id, step.fingerX, step.fingerY);
                step.fingerState = 1;
            }
            else if (step.fingerState == 1)
            {
                float t = (float)(now - step.startMs) / (float)(step.endMs - step.startMs);
                t = t > 1.0f ? 1.0f : t;
                bo1_native_touch_finger(now >= step.endMs ? 2 : 1, id, step.fingerX + (step.fingerX2 - step.fingerX) * t,
                    step.fingerY + (step.fingerY2 - step.fingerY) * t);
                if (now >= step.endMs)
                    step.fingerState = 2;
            }
            continue;
        }
        if (step.click)
        {
            if (now >= step.startMs && step.clickState == 0)
            {
                // absolute for a free pointer; while the engine holds the pointer its menu cursor moves by deltas:
                // far up-left to the corner first, then to the point
                bo1_native_input_focus(1);   // (a display without a window manager never says the window has the focus)
                bo1_native_input_motion(-4000, -4000, step.clickX, step.clickY, 1280, 720);
                step.clickState = 3;
            }
            else if (now >= step.startMs + 250 && step.clickState == 3)
            {
                bo1_native_input_motion(step.clickX, step.clickY, step.clickX, step.clickY, 1280, 720);
                step.clickState = 4;
            }
            else if (now >= step.startMs + 500 && step.clickState == 4)
            {
                bo1_native_input_buttons(1);
                step.clickState = 1;
            }
            else if (now >= step.endMs + 500 && step.clickState == 1)
            {
                bo1_native_input_buttons(0);
                step.clickState = 2;
            }
            continue;
        }
        if (now < step.startMs || now >= step.endMs)
            continue;
        s.buttons |= step.buttons;
        if (step.lt != 0) s.lt = step.lt;
        if (step.rt != 0) s.rt = step.rt;
        if (step.lx != 0) s.lx = step.lx;
        if (step.ly != 0) s.ly = step.ly;
        if (step.rx != 0) s.rx = step.rx;
        if (step.ry != 0) s.ry = step.ry;
    }
    bo1_native_touch_real_pad(s_testFingers ? 0 : 1);
    if (s_testFingers)
        MergeTouch(s);
    Publish(0, s);
}
}

extern "C" void bo1_native_gamepad_update(void)
{
    if (!s_init)
    {
        s_init = true;
        const char *spec = getenv("BO1_TESTPAD");
        if (spec && *spec)
        {
            s_testMode = true;
            s_testStart = SDL_GetTicks();
            ParseTestPad(spec);
        }
        else if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        {
            fprintf(stderr, "native: SDL_Init(GAMECONTROLLER) failed: %s\n", SDL_GetError());
        }
    }
    if (s_testMode)
    {
        UpdateTestPad();
        return;
    }
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        return;
    SDL_GameControllerUpdate();
    RefreshControllers();
    bo1_gamepad *pads = bo1_input_gamepads();
    bool anyController = false;
    for (int slot = 0; slot < BO1_MAX_GAMEPADS; ++slot)
        anyController |= s_controllers[slot] != nullptr;
    bo1_native_touch_real_pad(anyController);
    for (int slot = 0; slot < BO1_MAX_GAMEPADS; ++slot)
    {
        GPadRawState s{};
        SDL_GameController *c = s_controllers[slot];
        if (c)
        {
            s.connected = true;
            s.buttons = Buttons(c);
            s.lt = Axis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            s.rt = Axis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            s.lx = Axis(c, SDL_CONTROLLER_AXIS_LEFTX);
            s.ly = -Axis(c, SDL_CONTROLLER_AXIS_LEFTY);    // SDL: down is positive; the engine: up
            s.rx = Axis(c, SDL_CONTROLLER_AXIS_RIGHTX);
            s.ry = -Axis(c, SDL_CONTROLLER_AXIS_RIGHTY);
            // rumble the engine asked for (GPad_Backend_SetRumble writes it into the same entry)
            const float low = pads[slot].rumble_low, high = pads[slot].rumble_high;
            if (low != s_rumbleLow[slot] || high != s_rumbleHigh[slot])
            {
                s_rumbleLow[slot] = low;
                s_rumbleHigh[slot] = high;
                const float l = low < 0 ? 0 : low > 1 ? 1 : low, h = high < 0 ? 0 : high > 1 ? 1 : high;
                SDL_GameControllerRumble(c, (Uint16)(l * 65535.0f), (Uint16)(h * 65535.0f), l > 0 || h > 0 ? 5000 : 0);
            }
        }
        else if (slot == 0 && bo1_native_touch_is_pad())
        {
            // no controller: the on-screen touch controls are the pad
            s.connected = true;
            MergeTouch(s);
        }
        if (slot == 0)
            bo1_native_second_screen_frame(&s);   // mod (native): the second screen's pads (native_second_screen.cpp)
        Publish(slot, s);
    }
}
