// native_video.cpp - the native build's window, OpenGL ES 3 context and keyboard / mouse events, on SDL2.
//
// The D3D9 shim (src/web/d3d9) asks for its context through d3d9shim_host (d3d9shim.h); the engine's input code
// (src/web/web_input.cpp) keeps its event queue and gets the events through bo1_native_input_*. SDL's video and
// event calls all happen on one thread: the engine's main thread, which creates the device (r_smp_backend 0, set in
// web_main.cpp) and pumps input (Win_GetEvent).
//
// Not compiled into this file: sound, game controllers (the engine's gamepad backend is separate).
#include "native_video.h"
#include "native_touch.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <d3d9shim.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern "C" {
// src/web/compat/win_gui.cpp: the fake window's client size
void bo1_web_canvas_size(int *width, int *height);
void bo1_web_set_monitor_size(int width, int height);
// src/web/web_input.cpp
void bo1_native_input_key(int vk, int down, int extended, int console);
void bo1_native_input_char(unsigned ch);
void bo1_native_input_buttons(int mask);
void bo1_native_input_wheel(int direction);
void bo1_native_input_focus(int active);
void bo1_native_input_motion(int dx, int dy, int x, int y, int width, int height);
int bo1_native_input_wants_grab(void);
int bo1_native_ui_active(void);
void bo1_native_input_enginekey(int key, int down);
void bo1_native_input_set_grabbed(int grabbed);
// src/web/compat/win_crt.cpp
__attribute__((noreturn)) void bo1_web_exit(int code);
}

namespace
{
SDL_Window *s_window;
SDL_GLContext s_context;
pthread_t s_videoThread;
bool s_videoInit;
bool s_grabbed;

bool InitVideo()
{
    if (s_videoInit)
        return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "native: SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
        return false;
    }
    s_videoInit = true;
    s_videoThread = pthread_self();
    return true;
}

// Android: the window is the display, whatever size was asked for. The shim scales the engine's back buffer into it.
// Elsewhere the window follows the engine's mode, unless BO1_WINDOW_SIZE=<width>x<height> fixes it (the same scaled
// present, for trying it on a desktop).
bool s_fixedWindow;
#ifdef __ANDROID__
const bool s_displayWindow = true;
#else
#define s_displayWindow s_fixedWindow
#endif

void UpdatePresentSize()
{
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(s_window, &w, &h);
    d3d9shim_set_present_size(w, h);
    printf("native: display %dx%d\n", w, h);
}

int CreateContext(int width, int height)
{
    if (!InitVideo())
        return 0;
    int w = 0, h = 0;
    bo1_web_canvas_size(&w, &h);   // the size the engine gave its window, when it already has one
    if (w <= 0 || h <= 0)
    {
        w = width;
        h = height;
    }
    if (s_window && s_context)
    {
        if (!s_displayWindow)
            SDL_SetWindowSize(s_window, w, h);
        return SDL_GL_MakeCurrent(s_window, s_context) == 0;
    }
#ifndef __ANDROID__
    if (const char *fixed = SDL_getenv("BO1_WINDOW_SIZE"))
    {
        int fw = 0, fh = 0;
        if (sscanf(fixed, "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0)
        {
            s_fixedWindow = true;
            w = fw;
            h = fh;
        }
    }
#endif
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_ShowCursor(SDL_DISABLE);   // no system pointer over the game (a mouse or a touch pad still moves the game's own)
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    // the shim renders into its own framebuffers and blits: the window needs color only
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __ANDROID__
    s_window = SDL_CreateWindow("Call of Duty: Black Ops", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
#else
    s_window = SDL_CreateWindow("Call of Duty: Black Ops", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
#endif
    if (!s_window)
    {
        fprintf(stderr, "native: SDL_CreateWindow(%dx%d, OpenGL ES 3.0) failed: %s\n", w, h, SDL_GetError());
        return 0;
    }
    s_context = SDL_GL_CreateContext(s_window);
    if (!s_context)
    {
        fprintf(stderr, "native: SDL_GL_CreateContext (OpenGL ES 3.0) failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
        return 0;
    }
    SDL_GL_MakeCurrent(s_window, s_context);
    SDL_GL_SetSwapInterval(0);   // the engine paces itself (com_maxfps)
    if (s_displayWindow)
        UpdatePresentSize();
#ifndef __ANDROID__
    SDL_StartTextInput();   // (not on Android: it raises the on-screen keyboard)
#endif
    return 1;
}

void Resize(int width, int height)
{
    if (s_window && width > 0 && height > 0 && !s_displayWindow)
        SDL_SetWindowSize(s_window, width, height);
}

void PumpEvents();
int s_buttonMask;          // mouse buttons held, as the engine was told
Uint32 s_realMouseAt;      // when a mouse that is not a touch last moved or clicked (SDL ticks; 0: never)
int s_tapDown, s_tapUp;    // frames until a touch's click / release is sent (0: none due)

void Swap()
{
    if (s_window)
        SDL_GL_SwapWindow(s_window);
    // The window lives on the thread that created the device, which is the engine's render thread
    // (RB_RenderThread runs R_Init even with r_smp_backend 0): SDL's events are polled here, once per frame.
    PumpEvents();
    bo1_native_gamepad_update();
}

// SDL scancode -> Windows virtual key (what win_wndproc.cpp's virtualKeyConvert is indexed with) + the "extended" bit
int ScancodeToVK(SDL_Scancode sc, int *extended, int *console)
{
    *extended = 0;
    *console = 0;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return '1' + (sc - SDL_SCANCODE_1);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return 0x70 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        return 0x61 + (sc - SDL_SCANCODE_KP_1);
    switch (sc)
    {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_GRAVE: *console = 1; return 0xC0;
    case SDL_SCANCODE_RETURN: return 0x0D;
    case SDL_SCANCODE_KP_ENTER: *extended = 1; return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_AC_BACK: return 0x1B;   // Android's back button
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_MINUS: return 0xBD;
    case SDL_SCANCODE_EQUALS: return 0xBB;
    case SDL_SCANCODE_LEFTBRACKET: return 0xDB;
    case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;
    case SDL_SCANCODE_BACKSLASH: return 0xDC;
    case SDL_SCANCODE_SEMICOLON: return 0xBA;
    case SDL_SCANCODE_APOSTROPHE: return 0xDE;
    case SDL_SCANCODE_COMMA: return 0xBC;
    case SDL_SCANCODE_PERIOD: return 0xBE;
    case SDL_SCANCODE_SLASH: return 0xBF;
    case SDL_SCANCODE_CAPSLOCK: return 0x14;
    case SDL_SCANCODE_PAUSE: return 0x13;
    case SDL_SCANCODE_INSERT: *extended = 1; return 0x2D;
    case SDL_SCANCODE_DELETE: *extended = 1; return 0x2E;
    case SDL_SCANCODE_HOME: *extended = 1; return 0x24;
    case SDL_SCANCODE_END: *extended = 1; return 0x23;
    case SDL_SCANCODE_PAGEUP: *extended = 1; return 0x21;
    case SDL_SCANCODE_PAGEDOWN: *extended = 1; return 0x22;
    case SDL_SCANCODE_LEFT: *extended = 1; return 0x25;
    case SDL_SCANCODE_UP: *extended = 1; return 0x26;
    case SDL_SCANCODE_RIGHT: *extended = 1; return 0x27;
    case SDL_SCANCODE_DOWN: *extended = 1; return 0x28;
    case SDL_SCANCODE_LSHIFT: return 0x10;
    case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: return 0x11;
    case SDL_SCANCODE_RCTRL: *extended = 1; return 0x11;
    case SDL_SCANCODE_LALT: return 0x12;
    case SDL_SCANCODE_RALT: *extended = 1; return 0x12;
    case SDL_SCANCODE_KP_0: return 0x60;
    case SDL_SCANCODE_KP_PERIOD: return 0x6E;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6A;
    case SDL_SCANCODE_KP_PLUS: return 0x6B;
    case SDL_SCANCODE_KP_MINUS: return 0x6D;
    case SDL_SCANCODE_KP_DIVIDE: *extended = 1; return 0x6F;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
    case SDL_SCANCODE_SCROLLLOCK: return 0x91;
    default: return 0;
    }
}

// SDL button mask -> the engine's: 1 left, 2 right, 4 middle, 8 X1, 16 X2
int ButtonMask(Uint32 sdl)
{
    int m = 0;
    if (sdl & SDL_BUTTON_LMASK) m |= 1;
    if (sdl & SDL_BUTTON_RMASK) m |= 2;
    if (sdl & SDL_BUTTON_MMASK) m |= 4;
    if (sdl & SDL_BUTTON_X1MASK) m |= 8;
    if (sdl & SDL_BUTTON_X2MASK) m |= 16;
    return m;
}
}

// The Vulkan renderer's window (src/native/vk/bo1vk_dxvk.cpp): the same window as CreateContext makes, without a GL
// context. DXVK's SDL2 window system creates the surface and the swap chain on it. Called again on a mode change.
// On Android the window is the display and DXVK stretches the back buffer over it.
// Before DXVK creates its Vulkan instance: SDL's video subsystem and its Vulkan loader, which DXVK's SDL2 window
// system asks for the instance extensions. The engine creates a first D3D9 object early, to read the adapter's
// name (Sys_DetectVideoCard), on another thread than the window's.
extern "C" int bo1_native_video_vulkan_prepare(void)
{
    if (!InitVideo())
        return 0;
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0)
    {
        fprintf(stderr, "native: SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
}

extern "C" void *bo1_native_video_vulkan_window(int width, int height)
{
    if (!InitVideo())
        return nullptr;
    s_videoThread = pthread_self();   // the events are polled where the window is
    int w = 0, h = 0;
    bo1_web_canvas_size(&w, &h);
    if (w <= 0 || h <= 0)
    {
        w = width;
        h = height;
    }
    if (s_window)
    {
        if (!s_displayWindow)
            SDL_SetWindowSize(s_window, w, h);
        return s_window;
    }
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_ShowCursor(SDL_DISABLE);   // no system pointer over the game (a mouse or a touch pad still moves the game's own)
    s_window = SDL_CreateWindow("Call of Duty: Black Ops", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
#else
    if (const char *fixed = SDL_getenv("BO1_WINDOW_SIZE"))
    {
        int fw = 0, fh = 0;
        if (sscanf(fixed, "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0)
        {
            s_fixedWindow = true;
            w = fw;
            h = fh;
        }
    }
    s_window = SDL_CreateWindow("Call of Duty: Black Ops", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
#endif
    if (!s_window)
    {
        fprintf(stderr, "native: SDL_CreateWindow(%dx%d, Vulkan) failed: %s\n", w, h, SDL_GetError());
        return nullptr;
    }
#ifndef __ANDROID__
    SDL_StartTextInput();
#endif
    return s_window;
}

extern "C" void bo1_native_video_vulkan_close(void)
{
    if (s_window)
    {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }
    SDL_Vulkan_UnloadLibrary();
}

extern "C" void bo1_native_video_drawable_size(int *width, int *height)
{
    *width = *height = 0;
    if (s_window)
        SDL_Vulkan_GetDrawableSize(s_window, width, height);
}

// once per frame the Vulkan renderer presents (what Swap does for OpenGL ES)
extern "C" void bo1_native_video_frame(void)
{
    PumpEvents();
    bo1_native_gamepad_update();
}

// The Extras screen's name row (client_mp/cl_extras.cpp): Android shows / hides the on-screen keyboard; a desktop
// has text input on all the time.
extern "C" void bo1_native_video_text_input(int on)
{
#ifdef __ANDROID__
    if (on)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
#else
    (void)on;
#endif
}

// The mode a first start uses, when the platform has a preference (BO1_DEFAULT_MODE=<width>x<height>, set by the
// Android loader; r_init.cpp makes it r_mode's default). Copied: the environment is not below 4 GB.
extern "C" void bo1_native_video_default_mode(char *text, int size)
{
    const char *mode = SDL_getenv("BO1_DEFAULT_MODE");
    snprintf(text, (size_t)size, "%s", mode ? mode : "");
}

// Whether the menus draw their pointer (ui/ui_main.cpp): always on a desktop; on Android only for a few seconds
// after a real mouse moved (a touch screen and a game pad have no use for an arrow).
extern "C" int bo1_native_video_show_pointer(void)
{
#ifdef __ANDROID__
    return s_realMouseAt && SDL_GetTicks() - s_realMouseAt < 5000;
#else
    return 1;
#endif
}

extern "C" void bo1_native_video_register(void)
{
    // the display's size for the list of video modes (BO1_DISPLAY_SIZE=<width>x<height>; default 1920x1080)
    if (const char *display = SDL_getenv("BO1_DISPLAY_SIZE"))
    {
        int w = 0, h = 0;
        if (sscanf(display, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360)
        {
            d3d9shim_set_display_size(w, h);
            bo1_web_set_monitor_size(w, h);
            printf("native: display %dx%d, preferred mode %s\n", w, h, SDL_getenv("BO1_DEFAULT_MODE") ? SDL_getenv("BO1_DEFAULT_MODE") : "-");
        }
    }
    d3d9shim_host host;
    host.create_context = CreateContext;
    host.resize = Resize;
    host.swap = Swap;
    d3d9shim_set_host(&host);
}

extern "C" void bo1_native_video_pump(void)
{
    // Called from the engine's input pump (main thread). The events themselves are polled on the window's thread
    // (Swap above); this only covers a window that lives on the calling thread and is not presenting yet.
    if (!s_videoInit || !pthread_equal(pthread_self(), s_videoThread))
        return;
    PumpEvents();
}

namespace
{
#ifdef __ANDROID__
SDL_FingerID s_fingerId;
float s_fingerX, s_fingerY;   // swipe distance not yet turned into key presses
bool s_fingerMoved;
bool s_fingerOnMenu;
float s_fingerLastX, s_fingerLastY, s_fingerStartX;

// an engine key pressed and released. The front end's TV menus follow a game pad's d-pad and A (ui/keycodes.h:
// K_BUTTON_A 1, K_DPAD_UP 0x14, DOWN 0x15, LEFT 0x16, RIGHT 0x17), not the keyboard's arrows.
void TapKey(int key, int)
{
    bo1_native_input_enginekey(key, 1);
    bo1_native_input_enginekey(key, 0);
}
#endif

// A pointer position in the window, as a position in the engine's picture. When the window is not the back buffer's
// size (Android, BO1_WINDOW_SIZE) the picture is scaled into it with bars (d3d9shim_set_present_size): the same
// rectangle is taken out here, so that a touch lands on the menu item under the finger.
void PointerMoved(int dx, int dy, int x, int y)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(s_window, &w, &h);
    int bw = 0, bh = 0;
    bo1_web_canvas_size(&bw, &bh);
    if (s_displayWindow && bw > 0 && bh > 0 && w > 0 && h > 0)
    {
        int pw = w, ph = (int)((long long)w * bh / bw);
        if (ph > h)
        {
            ph = h;
            pw = (int)((long long)h * bw / bh);
        }
        x -= (w - pw) / 2;
        y -= (h - ph) / 2;
        // relative motion in the picture's pixels too
        dx = dx * bw / pw;
        dy = dy * bh / ph;
        w = pw;
        h = ph;
    }
    bo1_native_input_motion(dx, dy, x, y, w, h);
}

// A finger's place in the window (0..1) as a place in the game's picture (0..1; outside it in the bars): the
// rectangle PointerMoved uses.
void FingerToPicture(float fx, float fy, float *px, float *py)
{
    *px = fx;
    *py = fy;
    int w = 0, h = 0, bw = 0, bh = 0;
    if (!s_window)
        return;
    SDL_GetWindowSize(s_window, &w, &h);
    bo1_web_canvas_size(&bw, &bh);
    if (!s_displayWindow || bw <= 0 || bh <= 0 || w <= 0 || h <= 0)
        return;
    float pw = (float)w, ph = (float)w * (float)bh / (float)bw;
    if (ph > (float)h)
    {
        ph = (float)h;
        pw = (float)h * (float)bw / (float)bh;
    }
    *px = (fx * (float)w - ((float)w - pw) * 0.5f) / pw;
    *py = (fy * (float)h - ((float)h - ph) * 0.5f) / ph;
}

// Android: the menus' pointer rests outside the picture except while a finger taps or a real mouse moves it. A
// pointer left over an entry keeps that entry highlighted, against the d-pad or a swipe.
void ParkPointer()
{
    int w = 0, h = 0;
    if (!s_window)
        return;
    SDL_GetWindowSize(s_window, &w, &h);
    bo1_native_input_motion(0, 0, -64, -64, w, h);
}

void PumpEvents()
{
    // a tap's click and release, due this frame (see SDL_MOUSEBUTTONDOWN below)
    if (s_tapDown > 0 && --s_tapDown == 0)
    {
        s_buttonMask |= 1;
        bo1_native_input_buttons(s_buttonMask);
    }
    if (s_tapUp > 0 && --s_tapUp == 0)
    {
        s_buttonMask &= ~1;
        bo1_native_input_buttons(s_buttonMask);
#ifdef __ANDROID__
        ParkPointer();
#endif
    }
#ifdef __ANDROID__
    // the pointer starts outside the picture too: in the middle of the screen it would hold the menu's highlight
    static bool parked;
    if (!parked && s_window)
    {
        parked = true;
        ParkPointer();
    }
#endif
    // the game owns the mouse (it asks to recenter): relative mode; a menu: the system cursor
    const bool wantGrab = bo1_native_input_wants_grab() != 0;
    if (wantGrab != s_grabbed)
    {
        s_grabbed = wantGrab;
        SDL_SetRelativeMouseMode(wantGrab ? SDL_TRUE : SDL_FALSE);
        bo1_native_input_set_grabbed(wantGrab);
    }
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT:
            bo1_web_exit(0);
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            int extended, console;
            const int vk = ScancodeToVK(e.key.keysym.scancode, &extended, &console);
            if (vk)
                bo1_native_input_key(vk, e.type == SDL_KEYDOWN, extended, console);
            if (e.type == SDL_KEYDOWN)
            {
                // control characters the engine expects as WM_CHAR (text input reports printable ones only)
                const SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                    bo1_native_input_char(13);
                else if (k == SDLK_BACKSPACE)
                    bo1_native_input_char(8);
                else if (k == SDLK_TAB)
                    bo1_native_input_char(9);
                else if (k == SDLK_ESCAPE)
                    bo1_native_input_char(27);
            }
            break;
        }
        case SDL_TEXTINPUT:
            for (const unsigned char *p = (const unsigned char *)e.text.text; *p; ++p)
                if (*p < 0x80 && *p != '`' && *p != '~')   // the console key toggles the console, it is not typed
                    bo1_native_input_char(*p);
            break;
        case SDL_MOUSEMOTION:
#ifdef __ANDROID__
            // a finger dragged over a menu that holds the pointer is a swipe (below), not a mouse turning the view
            if (e.motion.which == SDL_TOUCH_MOUSEID && bo1_native_ui_active() && bo1_native_input_wants_grab())
                break;
            // ... and over the on-screen controls it is theirs (native_touch.cpp)
            if (e.motion.which == SDL_TOUCH_MOUSEID && bo1_native_touch_wants_fingers())
                break;
#endif
            if (e.motion.which != SDL_TOUCH_MOUSEID)
                s_realMouseAt = SDL_GetTicks() | 1u;
            PointerMoved(e.motion.xrel, e.motion.yrel, e.motion.x, e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
#ifdef __ANDROID__
            // On a menu that holds the pointer a tap is the game pad's A (SDL_FINGERUP below). The click SDL makes
            // of the same touch is dropped, or one tap would act twice (pass the title screen and choose the first
            // entry of the menu behind it). SDL sends the mouse event before the finger event.
            static bool touchClickDropped;
            if (e.button.which == SDL_TOUCH_MOUSEID)
            {
                if (e.type == SDL_MOUSEBUTTONDOWN)
                    touchClickDropped = (bo1_native_ui_active() && bo1_native_input_wants_grab()) || bo1_native_touch_wants_fingers();
                if (touchClickDropped)
                    break;
            }
#endif
            // The mask follows the events, not SDL_GetMouseState: a tap on a touch screen arrives as "down" and "up"
            // in one batch, and the state read here would already be "up" for both.
            const int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2
                : e.button.button == SDL_BUTTON_MIDDLE ? 4 : e.button.button == SDL_BUTTON_X1 ? 8 : 16;
            if (e.button.which == SDL_TOUCH_MOUSEID)
            {
                // A tap: the pointer goes to the finger now and the click follows two frames later, when the menu
                // has seen the pointer over the entry (a click in the same frame only moved the highlight: every
                // entry needed two taps). The release follows the press.
                if (e.type == SDL_MOUSEBUTTONDOWN)
                {
                    PointerMoved(0, 0, e.button.x, e.button.y);
                    s_tapDown = 2;
                }
                else
                    s_tapUp = s_tapDown > 0 ? s_tapDown + 2 : 1;
                break;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                // a touch has no motion before it: the pointer goes to the finger first
                PointerMoved(0, 0, e.button.x, e.button.y);
                s_buttonMask |= bit;
            }
            else
                s_buttonMask &= ~bit;
            bo1_native_input_buttons(s_buttonMask);
            break;
        }
        case SDL_MOUSEWHEEL:
            if (e.wheel.y)
                bo1_native_input_wheel(e.wheel.y > 0 ? 1 : -1);
            break;
#ifdef __ANDROID__
        // Touch on the menus that take no pointer (the front end's 3D TV menus hold the mouse to turn the view, and
        // are driven by the arrow keys on a PC): a swipe moves the selection, a tap is Enter. Menus with a pointer
        // get the tap as a click (SDL turns touches into mouse events) and nothing from here.
        case SDL_FINGERDOWN:
            // in a game the fingers belong to the on-screen controls
            if (bo1_native_touch_wants_fingers())
            {
                float px, py;
                FingerToPicture(e.tfinger.x, e.tfinger.y, &px, &py);
                bo1_native_touch_finger(0, (long long)e.tfinger.fingerId, px, py);
                s_fingerOnMenu = false;
                break;
            }
            s_fingerId = e.tfinger.fingerId;
            s_fingerX = s_fingerY = 0.0f;
            s_fingerMoved = false;
            s_fingerLastX = e.tfinger.x;
            s_fingerLastY = e.tfinger.y;
            s_fingerStartX = e.tfinger.x;
            // (the touch must begin on such a menu: the tap that passes the title screen is not also Enter on the
            // main menu that appears under it)
            s_fingerOnMenu = bo1_native_ui_active() && bo1_native_input_wants_grab();
            break;
        case SDL_FINGERMOTION:
        {
            float px, py;
            FingerToPicture(e.tfinger.x, e.tfinger.y, &px, &py);
            bo1_native_touch_finger(1, (long long)e.tfinger.fingerId, px, py);   // (ignored unless it holds a control)
        }
            if (bo1_native_touch_wants_fingers())
                break;
            // A touch that began on the right 60 % of such a menu turns the view, like the mouse does there (the
            // front end's room); one on the left 40 % moves the selection.
            if (e.tfinger.fingerId == s_fingerId && s_fingerOnMenu && s_fingerStartX > 0.4f && bo1_native_ui_active() && bo1_native_input_wants_grab())
            {
                const float dx = (e.tfinger.x - s_fingerLastX) * 1600.0f, dy = (e.tfinger.y - s_fingerLastY) * 900.0f;
                s_fingerLastX = e.tfinger.x;
                s_fingerLastY = e.tfinger.y;
                s_fingerX += dx;
                s_fingerY += dy;
                if (s_fingerX * s_fingerX + s_fingerY * s_fingerY > 12.0f * 12.0f)
                    s_fingerMoved = true;
                if (s_fingerMoved)
                {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(s_window, &w, &h);
                    bo1_native_input_motion((int)dx, (int)dy, -64, -64, w, h);   // (the pointer stays parked)
                }
                break;
            }
            if (e.tfinger.fingerId == s_fingerId && s_fingerOnMenu && bo1_native_ui_active() && bo1_native_input_wants_grab())
            {
                // (dx, dy are fractions of the window; a step every 9 % of its height)
                // (from the positions: dx / dy are not filled in by every SDL touch back end)
                s_fingerX += (e.tfinger.x - s_fingerLastX) * 16.0f / 9.0f;
                s_fingerY += e.tfinger.y - s_fingerLastY;
                s_fingerLastX = e.tfinger.x;
                s_fingerLastY = e.tfinger.y;
                const float step = 0.09f;
                while (s_fingerY <= -step) { TapKey(0x14, 1); s_fingerY += step; s_fingerMoved = true; }
                while (s_fingerY >= step) { TapKey(0x15, 1); s_fingerY -= step; s_fingerMoved = true; }
                while (s_fingerX <= -step) { TapKey(0x16, 1); s_fingerX += step; s_fingerMoved = true; }
                while (s_fingerX >= step) { TapKey(0x17, 1); s_fingerX -= step; s_fingerMoved = true; }
            }
            break;
        case SDL_FINGERUP:
        {
            float px, py;
            FingerToPicture(e.tfinger.x, e.tfinger.y, &px, &py);
            bo1_native_touch_finger(2, (long long)e.tfinger.fingerId, px, py);
        }
            if (bo1_native_touch_wants_fingers())
                break;
            if (SDL_getenv("BO1_TOUCH_TRACE"))
                fprintf(stderr, "touch: up at %.2f %.2f, on menu %d, moved %d\n", e.tfinger.x, e.tfinger.y, (int)s_fingerOnMenu, (int)s_fingerMoved);
            if (e.tfinger.fingerId == s_fingerId && !s_fingerMoved && s_fingerOnMenu && bo1_native_ui_active() && bo1_native_input_wants_grab())
            {
                TapKey(1, 0);
            }
            break;
#endif
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && s_displayWindow)
                UpdatePresentSize();
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                bo1_native_input_focus(1);
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                bo1_native_input_focus(0);
            break;
        default:
            break;
        }
    }
}
}
