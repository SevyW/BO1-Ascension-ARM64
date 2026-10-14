// cl_extras.cpp - mod (native menus): the "Extras" screen: settings of this build that the retail menus have no
// rows for, drawn by the engine over the settings menu.
//
// The retail menus come from the player's fastfiles. Their settings screen has an entry that does nothing in this
// build (Voice Chat: sv_voice 0); it is renamed EXTRAS and opens this screen instead (ui/ui_main.cpp renames it,
// ui/ui_shared.cpp Item_Action opens it). Also the console command "extras".
//
// Rows:
//   TOUCH CONTROLS      bo1_touch 0 / 1                        (client_mp/cl_touch.cpp)
//   TOUCH SENSITIVITY   bo1_touch_sensitivity, steps of 0.1
//   TOUCH SIZE          bo1_touch_scale, steps of 0.05
//   TOUCH OPACITY       bo1_touch_opacity, steps of 0.05
//   HOLD TO FIRE        bo1_touch_autofire 0 / 1: a held FIRE keeps firing pistols and other one-shot-per-pull weapons
//   EDIT TOUCH LAYOUT   closes this screen and starts touch_edit
//   RESET TOUCH LAYOUT  touch_reset
//   FRAME RATE LIMIT    com_maxfps 30 / 45 / 60 / 90 / 120 / 144 / 0 (no limit)
//   FPS COUNTER         bo1_fpscounter 0..3                    (client_mp/cl_scrn_mp.cpp)
//   RENDERER            bo1_renderer 0 / 1, at the next start  (web/web_input.cpp)
//   NEW SHADERS         bo1_vk_async 0 / 1, at the next start  (web/web_input.cpp; Vulkan only)
//   PLAY                the Play screen: maps and modes (cl_play.cpp)
//   BOT ENEMIES / BOT FRIENDS / BOT DIFFICULTY   bo1_bot_enemies / _friends / _difficulty, used by the Play screen
//   MOD                 the mod folder (<fs_basepath>/mods/<name> or <fs_homepath>/mods/<name>) the next start
//                       loads as fs_game; kept in <fs_homepath>/bo1_mod.txt, which web/web_main.cpp reads.
//                       Left / right choose, A restarts the game with it.
//   SEND LOG            Android: the end of bo1.log through the share sheet (native/native_extras.cpp)
//   PLAYER NAME         the name dvar; typed with the keyboard (Android: the on-screen one)
//   JOIN A GAME         the join-by-address screen (cl_joinmenu.cpp)
//
// Input: d-pad / arrows move, left / right change a value, A / Enter presses (a value steps up, wrapping), B /
// Escape closes. A mouse click or a tap on a row selects it; on its left half it steps the value down, on its
// right half up.
//
// Nothing here exists in the Windows build (BO1_NATIVE).
#include "cl_extras.h"

#ifdef BO1_NATIVE
#include "cl_joinmenu.h"
#include "cl_play.h"
#include <universal/com_files.h>
#include "cl_main_mp.h"
#include <client/client.h>
#include <client/screen_placement.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <ui/keycodes.h>
#include <ui/ui_main.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

extern "C" {
// src/native/native_touch.cpp, native_video.cpp
void bo1_native_touch_edit(int on);
void bo1_native_touch_reset(void);
void bo1_native_video_text_input(int on);
// src/native/native_extras.cpp
int bo1_native_list_dir(const char *path, int index, int wantFolders, char *name, int size);
int bo1_native_read_line(const char *path, char *text, int size);
int bo1_native_write_line(const char *path, const char *text);
void bo1_native_send_log(const char *homePath);
void bo1_native_restart_at_exit(const char *basePath, const char *homePath, const char *args);
}

namespace
{
enum RowKind { CHOICE, NUMBER, ACTION, TEXT, MOD };
enum ActionId { ACT_NONE, ACT_EDIT_TOUCH, ACT_RESET_TOUCH, ACT_JOIN, ACT_CLOSE, ACT_PLAY, ACT_LOG };

struct Row
{
    const char *label;
    RowKind kind;
    const char *dvar;       // the dvar shown and set
    const char *dvar2;      // a second dvar set to the same value (0: none)
    float low, high, step;  // NUMBER
    const char *const *names;   // CHOICE: what each value is called
    const int *values;      // CHOICE: the values (0: 0, 1, 2, ...)
    int count;
    ActionId action;
};

const char *const kOffOn[] = { "Off", "On" };
const char *const kFpsNames[] = { "30", "45", "60", "90", "120", "144", "No limit" };
const int kFpsValues[] = { 30, 45, 60, 90, 120, 144, 0 };
const char *const kCounter[] = { "None", "Simple", "Advanced", "Full" };
const char *const kRenderer[] = { "OpenGL ES", "Vulkan" };
const char *const kShaderCompile[] = { "Wait (stutter)", "Background (flicker once)" };   // bo1_vk_async 0 / 1
const char *const kBots[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
const char *const kDifficulty[] = { "Recruit", "Regular", "Hardened", "Veteran" };   // easy, normal, hard, fu

const Row kRows[] = {
    { "PLAY: MAPS AND MODES", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_PLAY },
    { "TOUCH CONTROLS", CHOICE, "bo1_touch", 0, 0, 0, 0, kOffOn, 0, 2, ACT_NONE },
    { "TOUCH SENSITIVITY", NUMBER, "bo1_touch_sensitivity", 0, 0.2f, 5.0f, 0.1f, 0, 0, 0, ACT_NONE },
    { "TOUCH SIZE", NUMBER, "bo1_touch_scale", 0, 0.5f, 2.0f, 0.05f, 0, 0, 0, ACT_NONE },
    { "TOUCH OPACITY", NUMBER, "bo1_touch_opacity", 0, 0.05f, 1.0f, 0.05f, 0, 0, 0, ACT_NONE },
    { "HOLD TO FIRE (SEMI-AUTO)", CHOICE, "bo1_touch_autofire", 0, 0, 0, 0, kOffOn, 0, 2, ACT_NONE },
    { "EDIT TOUCH LAYOUT", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_EDIT_TOUCH },
    { "RESET TOUCH LAYOUT", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_RESET_TOUCH },
    { "FRAME RATE LIMIT", CHOICE, "com_maxfps", 0, 0, 0, 0, kFpsNames, kFpsValues, 7, ACT_NONE },
    { "FPS COUNTER", CHOICE, "bo1_fpscounter", 0, 0, 0, 0, kCounter, 0, 4, ACT_NONE },
#ifdef BO1_DXVK
    { "RENDERER (RESTART)", CHOICE, "bo1_renderer", 0, 0, 0, 0, kRenderer, 0, 2, ACT_NONE },
    { "NEW SHADERS (RESTART)", CHOICE, "bo1_vk_async", 0, 0, 0, 0, kShaderCompile, 0, 2, ACT_NONE },
#endif
    { "BOT ENEMIES", CHOICE, "bo1_bot_enemies", 0, 0, 0, 0, kBots, 0, 10, ACT_NONE },
    { "BOT FRIENDS", CHOICE, "bo1_bot_friends", 0, 0, 0, 0, kBots, 0, 9, ACT_NONE },
    { "BOT DIFFICULTY", CHOICE, "bo1_bot_difficulty", 0, 0, 0, 0, kDifficulty, 0, 4, ACT_NONE },
    { "MOD (A: RESTART WITH IT)", MOD, 0, 0, 0, 0, 0, 0, 0, 0, ACT_NONE },
    { "PLAYER NAME", TEXT, "name", 0, 0, 0, 0, 0, 0, 0, ACT_NONE },
    { "JOIN A GAME BY ADDRESS", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_JOIN },
    { "SEND LOG", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_LOG },
    { "CLOSE", ACTION, 0, 0, 0, 0, 0, 0, 0, 0, ACT_CLOSE },
};
const int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

bool s_open;
int s_row;
bool s_typing;          // PLAYER NAME is being typed
char s_text[32];
cmd_function_s s_extrasCmd;
bool s_registered;
// where the rows were drawn, for the pointer
float s_rowX, s_rowY, s_rowW, s_rowH, s_rowPitch, s_rowSplit;

char s_mod[64];   // the mod of the next start ("" none)

const char *ModFile()
{
    return va("%s/bo1_mod.txt", fs_homepath ? fs_homepath->current.string : "");
}

// the index-th mod folder of both mods folders ("_stack" is the engine's own); false past the end
bool ModAt(int index, char *name, int size)
{
    const char *roots[2] = { fs_basepath ? fs_basepath->current.string : "", fs_homepath ? fs_homepath->current.string : "" };
    for (int root = 0; root < 2; ++root)
    {
        if (root == 1 && !I_stricmp(roots[0], roots[1]))
            break;
        char folder[512];
        Com_sprintf(folder, sizeof(folder), "%s/mods", roots[root]);
        for (int i = 0; i < 256 && bo1_native_list_dir(folder, i, 1, name, size); ++i)
        {
            if (!I_stricmp(name, "_stack"))
                continue;
            if (index-- == 0)
                return true;
        }
    }
    return false;
}

void StepMod(int direction)
{
    char name[64];
    int count = 0, current = -1;
    while (count < 256 && ModAt(count, name, (int)sizeof(name)))
    {
        if (!I_stricmp(name, s_mod))
            current = count;
        ++count;
    }
    // -1 is "none"
    int next = current + (direction < 0 ? -1 : 1);
    if (next >= count)
        next = -1;
    if (next < -1)
        next = count - 1;
    s_mod[0] = 0;
    if (next >= 0)
        ModAt(next, s_mod, (int)sizeof(s_mod));
    bo1_native_write_line(ModFile(), s_mod);
}

const char *Value(const char *dvar)
{
    const char *value = Dvar_GetVariantString(dvar);
    return value ? value : "";
}

void Set(const Row &row, const char *value)
{
    Cbuf_AddText(0, va("set %s \"%s\"\n", row.dvar, value));
    if (row.dvar2)
        Cbuf_AddText(0, va("set %s \"%s\"\n", row.dvar2, value));
}

int ChoiceIndex(const Row &row)
{
    const int current = atoi(Value(row.dvar));
    for (int i = 0; i < row.count; ++i)
        if ((row.values ? row.values[i] : i) == current)
            return i;
    return -1;
}

void StopTyping(bool keep)
{
    if (!s_typing)
        return;
    s_typing = false;
    bo1_native_video_text_input(0);
    if (keep && s_text[0])
        Cbuf_AddText(0, va("set name \"%s\"\n", s_text));
}

void Close()
{
    StopTyping(true);
    s_open = false;
}

// direction: +1 / -1 (left, right, the halves of a row), 0 = pressed (A, Enter)
void Change(int direction)
{
    const Row &row = kRows[s_row];
    switch (row.kind)
    {
    case CHOICE:
    {
        int index = ChoiceIndex(row);
        index = index < 0 ? 0 : (index + (direction < 0 ? row.count - 1 : 1)) % row.count;
        Set(row, va("%d", row.values ? row.values[index] : index));
        break;
    }
    case NUMBER:
    {
        float value = (float)atof(Value(row.dvar)) + (direction < 0 ? -row.step : row.step);
        if (direction == 0 && value > row.high + row.step * 0.5f)
            value = row.low;   // pressing steps up and wraps
        value = value < row.low ? row.low : value > row.high ? row.high : value;
        Set(row, va("%.2f", value));
        break;
    }
    case TEXT:
        if (direction == 0)
        {
            if (s_typing)
            {
                StopTyping(true);
            }
            else
            {
                s_typing = true;
                I_strncpyz(s_text, Value(row.dvar), sizeof(s_text));
                bo1_native_video_text_input(1);
            }
        }
        break;
    case MOD:
        if (direction != 0)
        {
            StepMod(direction);
        }
        else
        {
            // start again in the same mode; the mod file is read at the start
            Close();
            bo1_native_restart_at_exit(fs_basepath->current.string, fs_homepath->current.string,
                Dvar_GetInt("bo1_zombies") ? "+set bo1_zombies 1" : "");
            Cbuf_AddText(0, "quit\n");
        }
        break;
    case ACTION:
        if (direction != 0)
            break;
        if (row.action == ACT_EDIT_TOUCH)
        {
            Close();
            bo1_native_touch_edit(1);
        }
        else if (row.action == ACT_RESET_TOUCH)
        {
            bo1_native_touch_reset();
        }
        else if (row.action == ACT_JOIN)
        {
            Close();
            CL_JoinMenu_Open();
        }
        else if (row.action == ACT_PLAY)
        {
            Close();
            CL_Play_Open(-1);
        }
        else if (row.action == ACT_LOG)
        {
            bo1_native_send_log(fs_homepath ? fs_homepath->current.string : "");
        }
        else if (row.action == ACT_CLOSE)
        {
            Close();
        }
        break;
    }
}

// the UI's pointer in pixels of the picture
void Pointer(float *x, float *y)
{
    const uiInfo_s *ui = &uiInfoArray[0];
    *x = ui->uiDC.cursor.x * ScrPlace_HiResGetScale() * scrPlaceFull.scaleVirtualToFull[0];
    *y = ui->uiDC.cursor.y * ScrPlace_HiResGetScale() * scrPlaceFull.scaleVirtualToFull[1];
}

void Click()
{
    float x, y;
    Pointer(&x, &y);
    if (s_rowPitch <= 0.0f || x < s_rowX || x > s_rowX + s_rowW || y < s_rowY)
        return;
    const int row = (int)((y - s_rowY) / s_rowPitch);
    if (row < 0 || row >= kRowCount || y - s_rowY - (float)row * s_rowPitch > s_rowH)
        return;
    if (s_typing && row != s_row)
        StopTyping(true);
    s_row = row;
    const RowKind kind = kRows[row].kind;
    if (kind == ACTION || kind == TEXT)
        Change(0);
    else if (kind == MOD && x > s_rowX + s_rowW * 0.9f)
        Change(0);   // the row's right end: restart with it
    else
        Change(x < s_rowSplit ? -1 : 1);
}

void OpenCommand()
{
    CL_Extras_Open();
}
}

void CL_Extras_Open()
{
    s_open = true;
    bo1_native_read_line(ModFile(), s_mod, (int)sizeof(s_mod));
    s_typing = false;
}

bool CL_Extras_IsOpen()
{
    return s_open;
}

// a key going down: true when this screen took it
bool CL_Extras_Key(int localClientNum, int key)
{
    (void)localClientNum;
    if (!s_open)
        return false;
    if (s_typing)
    {
        if (key == K_ENTER || key == K_KP_ENTER || key == K_BUTTON_A || key == K_BUTTON_START)
            StopTyping(true);
        else if (key == K_ESCAPE || key == K_BUTTON_B)
            StopTyping(false);
        else if (key == K_BACKSPACE && s_text[0])
            s_text[strlen(s_text) - 1] = 0;
        else if (key == K_MOUSE1)
            Click();
        return true;
    }
    switch (key)
    {
    case K_DPAD_UP:
    case K_UPARROW:
        s_row = (s_row + kRowCount - 1) % kRowCount;
        break;
    case K_DPAD_DOWN:
    case K_DOWNARROW:
        s_row = (s_row + 1) % kRowCount;
        break;
    case K_DPAD_LEFT:
    case K_LEFTARROW:
        Change(-1);
        break;
    case K_DPAD_RIGHT:
    case K_RIGHTARROW:
        Change(1);
        break;
    case K_BUTTON_A:
    case K_ENTER:
    case K_KP_ENTER:
        Change(0);
        break;
    case K_MOUSE1:
        Click();
        break;
    case K_BUTTON_B:
    case K_ESCAPE:
        Close();
        break;
    default:
        break;
    }
    return true;
}

bool CL_Extras_Char(int character)
{
    if (!s_open)
        return false;
    if (s_typing && character >= 32 && character < 127 && character != '"' && character != ';' && character != '%')
    {
        const size_t length = strlen(s_text);
        if (length + 1 < sizeof(s_text))
        {
            s_text[length] = (char)character;
            s_text[length + 1] = 0;
        }
    }
    return true;
}

// CL_DrawScreen, every frame
void CL_Extras_Draw()
{
    if (!s_registered)
    {
        s_registered = true;
        Cmd_AddCommandInternal("extras", OpenCommand, &s_extrasCmd);
    }
    if (!s_open || !cls.whiteMaterial)
        return;
    Font_s *font = UI_GetFontHandle(&scrPlaceFull, 0, 0.25f);
    if (!font)
        font = cls.consoleFont;
    if (!font)
        return;
    static const float shade[4] = { 0.0f, 0.0f, 0.0f, 0.80f };
    static const float panel[4] = { 0.10f, 0.11f, 0.12f, 0.97f };
    static const float cell[4] = { 0.20f, 0.21f, 0.23f, 1.0f };
    static const float selected[4] = { 0.85f, 0.55f, 0.10f, 1.0f };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float dim[4] = { 0.72f, 0.72f, 0.72f, 1.0f };

    const float screenW = scrPlaceFull.realViewportSize[0], screenH = scrPlaceFull.realViewportSize[1];
    if (screenW <= 0.0f || screenH <= 0.0f)
        return;
    const float u = screenH / 720.0f;   // the layout is for 720 lines
    const float fontHeight = (float)R_TextHeight(font);
    const float scale = fontHeight > 0.0f ? 22.0f * u / fontHeight : 1.0f;
    const float rowH = 26.0f * u, pitch = 30.0f * u;   // twenty rows on 720 lines
    const float w = 640.0f * u, h = 70.0f * u + pitch * (float)kRowCount + 40.0f * u;
    const float x = (screenW - w) * 0.5f, y = (screenH - h) * 0.5f;

    R_AddCmdDrawStretchPic(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 1.0f, 1.0f, shade, cls.whiteMaterial);
    R_AddCmdDrawStretchPic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, panel, cls.whiteMaterial);
    R_AddCmdDrawText("EXTRAS", 0x7FFFFFFF, font, x + 20.0f * u, y + 42.0f * u, scale * 1.25f, scale * 1.25f, 0.0f, white, 3);

    s_rowX = x + 20.0f * u;
    s_rowY = y + 62.0f * u;
    s_rowW = w - 40.0f * u;
    s_rowH = rowH;
    s_rowPitch = pitch;
    s_rowSplit = s_rowX + s_rowW * 0.72f;   // the middle of the value column
    const float valueX = s_rowX + s_rowW * 0.48f;
    for (int i = 0; i < kRowCount; ++i)
    {
        const Row &row = kRows[i];
        const float top = s_rowY + pitch * (float)i;
        R_AddCmdDrawStretchPic(s_rowX, top, s_rowW, rowH, 0.0f, 0.0f, 1.0f, 1.0f, i == s_row ? selected : cell, cls.whiteMaterial);
        R_AddCmdDrawText((char *)row.label, 0x7FFFFFFF, font, s_rowX + 12.0f * u, top + 21.0f * u, scale * 0.88f, scale * 0.88f, 0.0f, white, 3);
        const char *value = 0;
        if (row.kind == CHOICE)
        {
            const int index = ChoiceIndex(row);
            value = index >= 0 ? row.names[index] : Value(row.dvar);
        }
        else if (row.kind == NUMBER)
        {
            value = va("%.2f", atof(Value(row.dvar)));
        }
        else if (row.kind == MOD)
        {
            value = s_mod[0] ? s_mod : "None";
        }
        else if (row.kind == TEXT)
        {
            value = s_typing && i == s_row ? va("%s_", s_text) : Value(row.dvar);
        }
        if (value)
            R_AddCmdDrawText(va(row.kind == TEXT ? "%s" : "<  %s  >", value), 0x7FFFFFFF, font, valueX, top + 21.0f * u, scale * 0.88f, scale * 0.88f, 0.0f, white, 3);
    }
    R_AddCmdDrawText("Up / down: choose   Left / right: change   A: press   B: close", 0x7FFFFFFF, font, x + 20.0f * u,
        y + h - 14.0f * u, scale * 0.62f, scale * 0.62f, 0.0f, dim, 3);
}
#else
void CL_Extras_Open()
{
}

bool CL_Extras_IsOpen()
{
    return false;
}

bool CL_Extras_Key(int, int)
{
    return false;
}

bool CL_Extras_Char(int)
{
    return false;
}

void CL_Extras_Draw()
{
}
#endif
