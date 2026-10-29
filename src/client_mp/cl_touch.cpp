// cl_touch.cpp - mod (touch): the engine side of the on-screen touch controls of the native builds.
//
// The controls themselves (which finger does what, the layout) are src/native/native_touch.cpp; they stand in for a
// game pad. This file gives them their settings every frame and draws them: a translucent box per control with its
// label in the game's small font, over the HUD, while a game is being played and no menu is open.
//
// Dvars (archived):
//   bo1_touch              0 / 1, default 1 on Android and 0 elsewhere: the controls on or off. They hide by
//                          themselves while a game pad is connected.
//   bo1_touch_sensitivity  0.2 .. 5, default 1.2: how far the view turns for a finger's travel.
//   bo1_touch_scale        0.5 .. 2, default 1: the size of every control.
//   bo1_touch_opacity      0.05 .. 1, default 0.28.
//   bo1_touch_autofire     0 / 1, default 1: a held FIRE keeps firing a weapon that fires once per pull.
//   bo1_touch_autofire_rate  1 .. 8 pulls a second, default 5 (the weapon's own fire time still applies).
//   bo1_second_screen      0 / 1, default 1: mod (native): the trackpads on a dual-screen device's second display
//                          (src/native/native_second_screen.cpp, docs/second-screen.md).
// Commands: touch_edit (move the controls with a finger; DONE saves to <fs_homepath>/touch_layout.txt, RESET
// restores the defaults), touch_reset.
//
// Nothing here exists in the Windows build (BO1_NATIVE).
#include "cl_touch.h"

#ifdef BO1_NATIVE
#include "cl_joinmenu.h"
#include "cl_extras.h"
#include "cl_play.h"
#include "cl_main_mp.h"
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_def.h>
#include <cgame_mp/cg_local_mp.h>
#include <cgame_mp/cg_newDraw_mp.h>
#include <client/client.h>
#include <client/screen_placement.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <ui/ui_main.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <math.h>

// src/native/native_touch.h (not included: that directory is outside the rewritten tree's include paths for
// engine files; these declarations have no pointer-to-pointer parameters)
extern "C" {
void bo1_native_touch_settings(int enabled, int ingame, float sensitivity, float scale, float aspect);
void bo1_native_touch_layout_file(const char *path);
int bo1_native_touch_wants_fingers(void);
int bo1_native_touch_shape(int index, float *x, float *y, float *halfWidth, float *halfHeight, int *held, int *style,
    char *label, int labelSize);
void bo1_native_touch_weapon(int semiAuto, float pullsPerSecond);
void bo1_native_touch_edit(int on);
int bo1_native_touch_editing(void);
void bo1_native_touch_reset(void);
void bo1_native_second_screen_settings(int enabled, int ingame);
}

namespace
{
const dvar_t *bo1_touch;
const dvar_t *bo1_second_screen;
const dvar_t *bo1_touch_sensitivity;
const dvar_t *bo1_touch_scale;
const dvar_t *bo1_touch_opacity;
const dvar_t *bo1_touch_autofire;
const dvar_t *bo1_touch_autofire_rate;
cmd_function_s s_editCmd, s_resetCmd;
bool s_registered;
bool s_fileGiven;

// A disc of `fill` with a ring of `ring` around it (ringWidth: its share of the radius), centered at (cx, cy) with
// half extents (rx, ry), as horizontal strips.
void Disc(float cx, float cy, float rx, float ry, float ringWidth, const float *fill, const float *ring)
{
    // a strip every two lines of the picture: the edge is as smooth as boxes get
    const int strips = ry < 16.0f ? 16 : ry > 72.0f ? 72 : (int)ry;
    const float inner = 1.0f - ringWidth;
    for (int i = 0; i < strips; ++i)
    {
        const float t = ((float)i + 0.5f) / (float)strips * 2.0f - 1.0f;   // -1 .. 1, the strip's middle
        const float outerHalf = sqrtf(1.0f - t * t);
        const float innerHalf = fabsf(t) < inner ? sqrtf(inner * inner - t * t) : 0.0f;
        const float top = cy - ry + 2.0f * ry * (float)i / (float)strips, height = 2.0f * ry / (float)strips;
        if (innerHalf > 0.0f)
        {
            if (fill[3] > 0.0f)
                R_AddCmdDrawStretchPic(cx - innerHalf * rx, top, 2.0f * innerHalf * rx, height, 0.0f, 0.0f, 1.0f, 1.0f, fill, cls.whiteMaterial);
            R_AddCmdDrawStretchPic(cx - outerHalf * rx, top, (outerHalf - innerHalf) * rx, height, 0.0f, 0.0f, 1.0f, 1.0f, ring, cls.whiteMaterial);
            R_AddCmdDrawStretchPic(cx + innerHalf * rx, top, (outerHalf - innerHalf) * rx, height, 0.0f, 0.0f, 1.0f, 1.0f, ring, cls.whiteMaterial);
        }
        else
        {
            R_AddCmdDrawStretchPic(cx - outerHalf * rx, top, 2.0f * outerHalf * rx, height, 0.0f, 0.0f, 1.0f, 1.0f, ring, cls.whiteMaterial);
        }
    }
}

void EditCommand()
{
    bo1_native_touch_edit(!bo1_native_touch_editing());
}

void ResetCommand()
{
    bo1_native_touch_reset();
}
}

// CL_DrawScreen, every frame
void CL_Touch_Draw()
{
    if (!s_registered)
    {
        s_registered = true;
#ifdef __ANDROID__
        const int on = 1;
#else
        const int on = 0;
#endif
        bo1_touch = _Dvar_RegisterInt("bo1_touch", on, 0, 1, 1u, "mod (touch): on-screen touch controls");
        bo1_touch_sensitivity = _Dvar_RegisterFloat("bo1_touch_sensitivity", 1.2f, 0.2f, 5.0f, 1u, "mod (touch): look sensitivity of a finger");
        bo1_touch_scale = _Dvar_RegisterFloat("bo1_touch_scale", 1.0f, 0.5f, 2.0f, 1u, "mod (touch): size of the on-screen controls");
        bo1_touch_opacity = _Dvar_RegisterFloat("bo1_touch_opacity", 0.28f, 0.05f, 1.0f, 1u, "mod (touch): opacity of the on-screen controls");
        bo1_touch_autofire = _Dvar_RegisterInt("bo1_touch_autofire", 1, 0, 1, 1u, "mod (touch): a held FIRE keeps firing weapons that fire once per pull");
        bo1_touch_autofire_rate = _Dvar_RegisterFloat("bo1_touch_autofire_rate", 5.0f, 1.0f, 8.0f, 1u, "mod (touch): trigger pulls per second of a held FIRE");
        // mod (native): the second-screen controller (Settings > EXTRAS)
        bo1_second_screen = _Dvar_RegisterInt("bo1_second_screen", 1, 0, 1, 1u, "mod (native): trackpads and grip buttons on a dual-screen device's second display");
        Cmd_AddCommandInternal("touch_edit", EditCommand, &s_editCmd);
        Cmd_AddCommandInternal("touch_reset", ResetCommand, &s_resetCmd);
    }
    if (!s_fileGiven && fs_homepath && fs_homepath->current.string && fs_homepath->current.string[0])
    {
        s_fileGiven = true;
        bo1_native_touch_layout_file(va("%s/touch_layout.txt", fs_homepath->current.string));
    }
    const float screenW = scrPlaceFull.realViewportSize[0] > 0.0f ? scrPlaceFull.realViewportSize[0] : 1280.0f;
    const float screenH = scrPlaceFull.realViewportSize[1] > 0.0f ? scrPlaceFull.realViewportSize[1] : 720.0f;
    // a game being played with nothing in front of it: no menu, console or chat line has the keys
    const connstate_t state = CL_GetLocalClientConnectionState(0);
    clientUIActive_t *ui = CL_GetLocalClientUIGlobals(0);
    const bool ingame = state == CA_ACTIVE && !Com_IsSPFrontEndRunning() && ui && !ui->keyCatchers && !CL_JoinMenu_IsOpen() && !CL_Extras_IsOpen() && !CL_Play_IsOpen();
    bo1_native_touch_settings(bo1_touch->current.integer, ingame, bo1_touch_sensitivity->current.value,
        bo1_touch_scale->current.value, screenW / screenH);
    bo1_native_second_screen_settings(bo1_second_screen->current.integer, ingame);   // mod (native)
    // the weapon in hand: full-automatic weapons (and the minigun) fire while the trigger is held; the others
    // once per pull
    int semiAuto = 0;
    if (ingame && cgArray)
    {
        const unsigned int weapon = cgArray[0].predictedPlayerState.weapon;
        const WeaponDef *def = weapon ? BG_GetWeaponDef(weapon) : 0;
        semiAuto = def && def->fireType != WEAPON_FIRETYPE_FULLAUTO && def->fireType != WEAPON_FIRETYPE_MINIGUN;
    }
    bo1_native_touch_weapon(semiAuto, bo1_touch_autofire->current.integer ? bo1_touch_autofire_rate->current.value : 0.0f);
    if (!bo1_native_touch_wants_fingers() || !cls.rendererStarted || !cls.whiteMaterial)
        return;

    Font_s *font = UI_GetFontHandle(&scrPlaceFull, 0, 0.25f);
    if (!font)
        font = cls.consoleFont;
    const float fontHeight = font ? (float)R_TextHeight(font) : 0.0f;
    const float opacity = bo1_touch_opacity->current.value;
    const bool editing = bo1_native_touch_editing() != 0;
    if (editing)
    {
        static const float shade[4] = { 0.0f, 0.0f, 0.0f, 0.45f };
        R_AddCmdDrawStretchPic(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 1.0f, 1.0f, shade, cls.whiteMaterial);
    }
    for (int index = 0;; ++index)
    {
        float x = 0.0f, y = 0.0f, halfW = 0.0f, halfH = 0.0f;
        int held = 0, style = 0;
        char label[16];
        label[0] = 0;
        if (!bo1_native_touch_shape(index, &x, &y, &halfW, &halfH, &held, &style, label, (int)sizeof(label)))
            break;
        const float cx = x * screenW, cy = y * screenH, rx = halfW * screenW, ry = halfH * screenH;
        float strength = editing && opacity < 0.5f ? 0.5f : opacity;   // 0.28: a quiet overlay
        strength = strength * 3.0f > 1.0f ? 1.0f : strength * 3.0f;    // the ring and the label
        // dark glass with a light ring; orange (the game's menu color) while held; a red ring on the fire buttons
        float fill[4] = { 0.0f, 0.0f, 0.0f, opacity * 0.9f };
        float ring[4] = { 1.0f, 1.0f, 1.0f, strength * 0.75f };
        float ink[4] = { 1.0f, 1.0f, 1.0f, strength };
        float ringWidth = 0.09f;
        if (style == 1)
        {
            ring[1] = 0.45f;
            ring[2] = 0.35f;
        }
        if (style == 2)
        {
            fill[3] = opacity * 0.35f;
            ringWidth = 0.05f;
        }
        if (style == 3)
        {
            fill[0] = fill[1] = fill[2] = 1.0f;
            fill[3] = strength * 0.6f;
        }
        else if (held)
        {
            fill[0] = 0.90f;
            fill[1] = 0.52f;
            fill[2] = 0.10f;
            fill[3] = strength * 0.7f;
            ring[0] = 1.0f;
            ring[1] = 0.75f;
            ring[2] = 0.30f;
            ring[3] = strength;
        }
        Disc(cx, cy, rx, ry, ringWidth, fill, ring);
        if (!label[0] || !font || fontHeight <= 0.0f)
            continue;
        // the label: under a third of the button's height, narrowed to fit inside the ring
        float scale = ry * 0.62f / fontHeight;
        const float textWidth = (float)R_TextWidth(label, 0x7FFFFFFF, font);
        if (textWidth * scale > rx * 1.45f && textWidth > 0.0f)
            scale = rx * 1.45f / textWidth;
        R_AddCmdDrawText(label, 0x7FFFFFFF, font, cx - textWidth * scale * 0.5f, cy + fontHeight * scale * 0.5f, scale,
            scale, 0.0f, ink, 3);
    }
}
#else
void CL_Touch_Draw()
{
}
#endif
