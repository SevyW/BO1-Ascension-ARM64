// cl_play.cpp - mod (native menus): the "Play" screen: start a map in any of the modes this build plays, drawn by
// the engine over the menus.
//
// Why: the retail ways in need the platform's online service (Multiplayer's Play entry is disabled without it,
// Zombies > Online asks for a network login), and a custom map has no entry at all. This screen starts
//   ZOMBIES: SOLO                 map <name>
//   ZOMBIES: HOST                 the same as a listen server others join by address (systemlink 1, sv_maxclients 4,
//                                 bo1_expected_players 1, bo1_lobby_chars "0 1 2 3"); the screen shows this device's
//                                 address, and the join screens of other devices on the network find it
//   MULTIPLAYER: BOTS             an offline match: splitscreen_playerCount 1, splitscreen_botFriends / botEnemies /
//                                 botDifficulty (docs/android-port.md, "Multiplayer with bots")
//   MULTIPLAYER: COMBAT TRAINING  onlinegame 1, xblive_basictraining 1, bot_friends / bot_enemies / bot_difficulty,
//                                 with the local profile (web/web_stats.cpp)
// Maps: the fastfiles of <fs_basepath>/zone/Common whose names start with zombie (without _patch / _load) or mp_,
// and for Zombies also the fastfiles of the active mod's folder (fs_game), which is where a custom map is.
// Bots: bo1_bot_enemies (0..9, default 6), bo1_bot_friends (0..8, default 5), bo1_bot_difficulty (0..3: easy,
// normal, hard, fu; default 1), archived; the Extras screen has rows for them.
//
// Zombies and Multiplayer are two modes of one program (bo1_zombies on the command line). Starting a map of the
// other mode starts the program again with the map's arguments (src/native/native_extras.cpp).
//
// Opened by the front end's Multiplayer entry (ui/ui_shared.cpp Item_Action), the Extras screen, and the console
// command "play". Input as on the Extras screen.
//
// Nothing here exists in the Windows build (BO1_NATIVE).
#include "cl_play.h"

#ifdef BO1_NATIVE
#include "cl_main_mp.h"
#include <client/client.h>
#include <client/screen_placement.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <ui/keycodes.h>
#include <ui/ui_main.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

extern "C" {
// src/native/native_extras.cpp
int bo1_native_list_dir(const char *path, int index, int wantFolders, char *name, int size);
void bo1_native_local_address(char *text, int size);
void bo1_native_restart_at_exit(const char *basePath, const char *homePath, const char *args);
}

namespace
{
enum Mode { ZM_SOLO, ZM_HOST, MP_BOTS, MP_TRAINING, MODE_COUNT };
const char *const kModeNames[MODE_COUNT] = { "ZOMBIES: SOLO", "ZOMBIES: HOST", "MULTIPLAYER: BOTS", "MULTIPLAYER: COMBAT TRAINING" };
const char *const kGameTypes[2] = { "tdm", "dm" };
const char *const kGameTypeNames[2] = { "Team Deathmatch", "Free-for-all" };
const char *const kDifficulty[4] = { "easy", "normal", "hard", "fu" };

const struct
{
    const char *map, *title;
} kTitles[] = {
    { "zombie_theater", "Kino der Toten" }, { "zombie_pentagon", "\"Five\"" }, { "zombietron", "Dead Ops Arcade" },
    { "zombie_cosmodrome", "Ascension" }, { "zombie_coast", "Call of the Dead" }, { "zombie_temple", "Shangri-La" },
    { "zombie_moon", "Moon" }, { "zombie_cod5_prototype", "Nacht der Untoten" }, { "zombie_cod5_asylum", "Verruckt" },
    { "zombie_cod5_sumpf", "Shi No Numa" }, { "zombie_cod5_factory", "Der Riese" },
    { "mp_array", "Array" }, { "mp_cracked", "Cracked" }, { "mp_crisis", "Crisis" }, { "mp_firingrange", "Firing Range" },
    { "mp_duga", "Grid" }, { "mp_hanoi", "Hanoi" }, { "mp_cairo", "Havana" }, { "mp_havoc", "Jungle" },
    { "mp_cosmodrome", "Launch" }, { "mp_nuked", "Nuketown" }, { "mp_radiation", "Radiation" }, { "mp_mountain", "Summit" },
    { "mp_villa", "Villa" }, { "mp_russianbase", "WMD" }, { "mp_berlinwall2", "Berlin Wall" }, { "mp_discovery", "Discovery" },
    { "mp_kowloon", "Kowloon" }, { "mp_stadium", "Stadium" }, { "mp_gridlock", "Convoy" }, { "mp_hotel", "Hotel" },
    { "mp_outskirts", "Stockpile" }, { "mp_zoo", "Zoo" }, { "mp_area51", "Hangar 18" }, { "mp_drivein", "Drive-In" },
    { "mp_golfcourse", "Hazard" }, { "mp_silo", "Silo" },
};

const int kMapMax = 96;
char s_zombieMaps[kMapMax][48], s_mpMaps[kMapMax][48];
int s_zombieCount, s_mpCount;
int s_zombieMap, s_mpMap;
int s_mode, s_gameType;
bool s_open;
int s_row;   // 0 mode, 1 game type, 2 map, 3 start, 4 close
const int kRowCount = 5;
cmd_function_s s_playCmd;
bool s_registered;
const dvar_t *bo1_bot_enemies;
const dvar_t *bo1_bot_friends;
const dvar_t *bo1_bot_difficulty;
float s_rowX, s_rowY, s_rowW, s_rowH, s_rowPitch, s_rowSplit;
char s_address[64];

bool IsZombies(int mode) { return mode == ZM_SOLO || mode == ZM_HOST; }

const char *Title(const char *map)
{
    for (const auto &entry : kTitles)
        if (!I_stricmp(entry.map, map))
            return entry.title;
    return map;
}

// "<name>.ff" of a map: not a patch, a load screen or a localized copy
bool MapOfFile(const char *file, char *map, int size)
{
    const int length = (int)strlen(file);
    if (length < 4 || length - 3 >= size || I_stricmp(file + length - 3, ".ff"))
        return false;
    I_strncpyz(map, file, length - 2);
    const int n = length - 3;
    if ((n > 6 && !I_stricmp(map + n - 6, "_patch")) || (n > 5 && !I_stricmp(map + n - 5, "_load")))
        return false;
    return true;
}

void Add(char list[][48], int *count, const char *map)
{
    for (int i = 0; i < *count; ++i)
        if (!I_stricmp(list[i], map))
            return;
    if (*count < kMapMax)
        I_strncpyz(list[(*count)++], map, 48);
}

void ScanFolder(const char *folder, bool anyName)
{
    char file[128], map[48];
    for (int index = 0; index < 4096 && bo1_native_list_dir(folder, index, 0, file, (int)sizeof(file)); ++index)
    {
        if (!MapOfFile(file, map, (int)sizeof(map)))
            continue;
        if (!I_strnicmp(map, "zombie", 6))
            Add(s_zombieMaps, &s_zombieCount, map);
        else if (!I_strnicmp(map, "mp_", 3))
            Add(s_mpMaps, &s_mpCount, map);
        else if (anyName && I_stricmp(map, "mod") && !(strlen(map) > 3 && map[2] == '_'))
            Add(s_zombieMaps, &s_zombieCount, map);   // a custom map of the mod, whatever it is called
    }
}

int Compare(const void *a, const void *b)
{
    return I_stricmp((const char *)a, (const char *)b);
}

void Scan()
{
    s_zombieCount = s_mpCount = 0;
    const char *base = fs_basepath ? fs_basepath->current.string : "";
    const char *home = fs_homepath ? fs_homepath->current.string : "";
    ScanFolder(va("%s/zone/Common", base), false);
    qsort(s_zombieMaps, s_zombieCount, sizeof(s_zombieMaps[0]), Compare);
    qsort(s_mpMaps, s_mpCount, sizeof(s_mpMaps[0]), Compare);
    const char *mod = Dvar_GetVariantString("fs_game");
    if (mod && mod[0])
    {
        ScanFolder(va("%s/%s", base, mod), true);
        ScanFolder(va("%s/%s", home, mod), true);
    }
    if (s_zombieMap >= s_zombieCount)
        s_zombieMap = 0;
    if (s_mpMap >= s_mpCount)
        s_mpMap = 0;
    // (the usual first choices)
    for (int i = 0; i < s_zombieCount && !s_zombieMap; ++i)
        if (!I_stricmp(s_zombieMaps[i], "zombie_theater"))
            s_zombieMap = i;
    for (int i = 0; i < s_mpCount && !s_mpMap; ++i)
        if (!I_stricmp(s_mpMaps[i], "mp_nuked"))
            s_mpMap = i;
    bo1_native_local_address(s_address, (int)sizeof(s_address));
}

const char *Map()
{
    if (IsZombies(s_mode))
        return s_zombieCount ? s_zombieMaps[s_zombieMap] : "";
    return s_mpCount ? s_mpMaps[s_mpMap] : "";
}


// the video settings of this mode, as "+set name value" arguments for a start in the other mode
void CarriedVideoArgs(char *video, int size)
{
    video[0] = 0;
    static const char *const kCarried[] = { "bo1_renderer", "r_mode", "r_aaSamples", "r_vsync", "r_displayRefresh", "com_maxfps",
        "bo1_fpscounter" };   // (few: the engine's command line holds 1024 bytes)
    for (const char *name : kCarried)
    {
        const char *value = Dvar_GetVariantString(name);
        if (value && value[0])
            I_strncat(video, size, va("+set %s \"%s\" ", name, value));
    }
}

void Start()
{
    const char *map = Map();
    if (!map[0])
        return;
    // name / value pairs, then the map
    char sets[1024];
    sets[0] = 0;
    const int enemies = bo1_bot_enemies->current.integer, friends = bo1_bot_friends->current.integer;
    const char *difficulty = kDifficulty[bo1_bot_difficulty->current.integer & 3];
    switch (s_mode)
    {
    case ZM_SOLO:
        I_strncat(sets, sizeof(sets), "systemlink 0|bo1_expected_players 0|");
        break;
    case ZM_HOST:
        I_strncat(sets, sizeof(sets), "systemlink 1|sv_maxclients 4|bo1_expected_players 1|bo1_lobby_chars \"0 1 2 3\"|sv_maxRate 25000|");
        break;
    case MP_BOTS:
        I_strncat(sets, sizeof(sets), va("onlinegame 0|xblive_basictraining 0|splitscreen_playerCount 1|splitscreen_botFriends %d|"
            "splitscreen_botEnemies %d|splitscreen_botDifficulty %s|", friends, enemies, difficulty));
        break;
    default:
        I_strncat(sets, sizeof(sets), va("onlinegame 1|xblive_basictraining 1|bot_friends %d|bot_enemies %d|bot_difficulty %s|",
            friends, enemies, difficulty));
        break;
    }
    if (!IsZombies(s_mode))
        I_strncat(sets, sizeof(sets), va("sv_maxclients 18|sv_botsAllowMovement 1|sv_botsPressAttackBtn 1|g_gametype %s|", kGameTypes[s_gameType]));

    // as console text ("set a b" lines) and as command line arguments ("+set a b")
    char console[1400], arguments[1400];
    console[0] = arguments[0] = 0;
    for (char *pair = sets; *pair;)
    {
        char *end = strchr(pair, '|');
        *end = 0;
        I_strncat(console, sizeof(console), va("set %s\n", pair));
        I_strncat(arguments, sizeof(arguments), va("+set %s ", pair));
        pair = end + 1;
    }
    I_strncat(console, sizeof(console), va("map %s\n", map));
    I_strncat(arguments, sizeof(arguments), va("+map %s", map));

    s_open = false;
    const bool zombiesNow = Dvar_GetInt("bo1_zombies") != 0;
    Com_Printf(0, "play: %s, %s\n", kModeNames[s_mode], map);
    if (zombiesNow == IsZombies(s_mode))
    {
        Cbuf_AddText(0, console);
        return;
    }
    // the other mode: quit (the configuration is written) and start again with the map's arguments
    const char *mod = Dvar_GetVariantString("fs_game");
    // Zombies and Multiplayer keep separate configurations (config.cfg, config_mp.cfg): the video settings chosen
    // in this mode go along, or the other one starts with its own old ones (seen: Vulkan and no anti-aliasing in
    // Zombies, then a Multiplayer match on OpenGL ES with 4 samples).
    char video[512];
    CarriedVideoArgs(video, sizeof(video));
    char all[2200];
    Com_sprintf(all, sizeof(all), "%s%s%s%s", IsZombies(s_mode) ? "+set bo1_zombies 1 " : "", mod && mod[0] ? va("+set fs_game %s ", mod) : "", video, arguments);
    bo1_native_restart_at_exit(fs_basepath->current.string, fs_homepath->current.string, all);
    Cbuf_AddText(0, "quit\n");
}

void Change(int direction)
{
    const int step = direction < 0 ? -1 : 1;
    switch (s_row)
    {
    case 0:
        s_mode = (s_mode + step + MODE_COUNT) % MODE_COUNT;
        break;
    case 1:
        if (!IsZombies(s_mode))
            s_gameType = (s_gameType + step + 2) % 2;
        break;
    case 2:
        if (IsZombies(s_mode) && s_zombieCount)
            s_zombieMap = (s_zombieMap + step + s_zombieCount) % s_zombieCount;
        else if (!IsZombies(s_mode) && s_mpCount)
            s_mpMap = (s_mpMap + step + s_mpCount) % s_mpCount;
        break;
    case 3:
        if (direction == 0)
            Start();
        break;
    default:
        if (direction == 0)
            s_open = false;
        break;
    }
}

void Click()
{
    const uiInfo_s *ui = &uiInfoArray[0];
    const float x = ui->uiDC.cursor.x * ScrPlace_HiResGetScale() * scrPlaceFull.scaleVirtualToFull[0];
    const float y = ui->uiDC.cursor.y * ScrPlace_HiResGetScale() * scrPlaceFull.scaleVirtualToFull[1];
    if (s_rowPitch <= 0.0f || x < s_rowX || x > s_rowX + s_rowW || y < s_rowY)
        return;
    const int row = (int)((y - s_rowY) / s_rowPitch);
    if (row < 0 || row >= kRowCount || y - s_rowY - (float)row * s_rowPitch > s_rowH)
        return;
    s_row = row;
    Change(row >= 3 ? 0 : x < s_rowSplit ? -1 : 1);
}

void Register()
{
    if (s_registered)
        return;
    s_registered = true;
    bo1_bot_enemies = _Dvar_RegisterInt("bo1_bot_enemies", 6, 0, 9, 1u, "mod: enemy bots of a match started from the Play screen");
    bo1_bot_friends = _Dvar_RegisterInt("bo1_bot_friends", 5, 0, 8, 1u, "mod: friendly bots of a match started from the Play screen");
    bo1_bot_difficulty = _Dvar_RegisterInt("bo1_bot_difficulty", 1, 0, 3, 1u, "mod: bot difficulty: 0 easy, 1 normal, 2 hard, 3 fu");
}

void OpenCommand()
{
    CL_Play_Open(-1);
}
}

// mode: 0 Zombies, 1 Multiplayer, -1 the mode the program runs in
void CL_Play_Open(int family)
{
    Register();
    Scan();
    const bool zombies = family < 0 ? Dvar_GetInt("bo1_zombies") != 0 : family == 0;
    if (zombies != IsZombies(s_mode))
        s_mode = zombies ? ZM_SOLO : MP_BOTS;
    s_row = 2;
    s_open = true;
}

bool CL_Play_IsOpen()
{
    return s_open;
}

// the front end's Multiplayer entry: the program starts again in Multiplayer mode, on its main menu (as retail's
// switch to the multiplayer executable), with this mode's video settings
void CL_Play_StartMultiplayerMenus()
{
    char video[512];
    CarriedVideoArgs(video, sizeof(video));
    const char *mod = Dvar_GetVariantString("fs_game");
    char all[1024];
    Com_sprintf(all, sizeof(all), "%s%s", mod && mod[0] ? va("+set fs_game %s ", mod) : "", video);
    Com_Printf(0, "play: Multiplayer menus\n");
    bo1_native_restart_at_exit(fs_basepath->current.string, fs_homepath->current.string, all);
    Cbuf_AddText(0, "quit\n");
}

bool CL_Play_Key(int localClientNum, int key)
{
    (void)localClientNum;
    if (!s_open)
        return false;
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
        Change(s_row >= 3 ? 0 : 1);
        break;
    case K_BUTTON_START:
        Start();
        break;
    case K_MOUSE1:
        Click();
        break;
    case K_BUTTON_B:
    case K_ESCAPE:
        s_open = false;
        break;
    default:
        break;
    }
    return true;
}

// CL_DrawScreen, every frame
void CL_Play_Draw()
{
    if (!s_registered)
    {
        Register();
        Cmd_AddCommandInternal("play", OpenCommand, &s_playCmd);
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
    const float u = screenH / 720.0f;
    const float fontHeight = (float)R_TextHeight(font);
    const float scale = fontHeight > 0.0f ? 22.0f * u / fontHeight : 1.0f;
    const float rowH = 40.0f * u, pitch = 46.0f * u;
    const float w = 760.0f * u, h = 70.0f * u + pitch * (float)kRowCount + 96.0f * u;
    const float x = (screenW - w) * 0.5f, y = (screenH - h) * 0.5f;

    R_AddCmdDrawStretchPic(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 1.0f, 1.0f, shade, cls.whiteMaterial);
    R_AddCmdDrawStretchPic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, panel, cls.whiteMaterial);
    R_AddCmdDrawText("PLAY", 0x7FFFFFFF, font, x + 20.0f * u, y + 42.0f * u, scale * 1.25f, scale * 1.25f, 0.0f, white, 3);

    s_rowX = x + 20.0f * u;
    s_rowY = y + 62.0f * u;
    s_rowW = w - 40.0f * u;
    s_rowH = rowH;
    s_rowPitch = pitch;
    s_rowSplit = s_rowX + s_rowW * 0.62f;
    const float valueX = s_rowX + s_rowW * 0.30f;
    const char *map = Map();
    for (int i = 0; i < kRowCount; ++i)
    {
        static const char *const labels[kRowCount] = { "MODE", "GAME TYPE", "MAP", "START", "CLOSE" };
        const float top = s_rowY + pitch * (float)i;
        R_AddCmdDrawStretchPic(s_rowX, top, s_rowW, rowH, 0.0f, 0.0f, 1.0f, 1.0f, i == s_row ? selected : cell, cls.whiteMaterial);
        R_AddCmdDrawText((char *)labels[i], 0x7FFFFFFF, font, s_rowX + 12.0f * u, top + 29.0f * u, scale, scale, 0.0f, white, 3);
        const char *value = 0;
        if (i == 0)
            value = kModeNames[s_mode];
        else if (i == 1)
            value = IsZombies(s_mode) ? 0 : kGameTypeNames[s_gameType];
        else if (i == 2)
            value = map[0] ? (I_stricmp(Title(map), map) ? va("%s  (%s)", Title(map), map) : map) : "no maps found";
        if (value)
            R_AddCmdDrawText(va("<  %s  >", value), 0x7FFFFFFF, font, valueX, top + 29.0f * u, scale, scale, 0.0f, white, 3);
    }
    float noteY = s_rowY + pitch * (float)kRowCount + 20.0f * u;
    const char *note = "";
    const bool zombiesNow = Dvar_GetInt("bo1_zombies") != 0;
    if (s_mode == ZM_HOST)
        note = s_address[0] ? va("Others join this device by address: %s (port 28960), or find it on their join screen.", s_address)
                            : "No network address found: connect to a network before hosting.";
    else if (!IsZombies(s_mode))
        note = va("Bots: %d enemies, %d friends, %s (Extras changes them).", bo1_bot_enemies->current.integer,
            bo1_bot_friends->current.integer, kDifficulty[bo1_bot_difficulty->current.integer & 3]);
    if (note[0])
        R_AddCmdDrawText((char *)note, 0x7FFFFFFF, font, x + 20.0f * u, noteY, scale * 0.72f, scale * 0.72f, 0.0f, dim, 3);
    noteY += 24.0f * u;
    if (zombiesNow != IsZombies(s_mode))
        R_AddCmdDrawText(va("START restarts the game in %s mode.", IsZombies(s_mode) ? "Zombies" : "Multiplayer"), 0x7FFFFFFF, font,
            x + 20.0f * u, noteY, scale * 0.72f, scale * 0.72f, 0.0f, dim, 3);
    R_AddCmdDrawText("Up / down: choose   Left / right: change   A: press   Start: start   B: close", 0x7FFFFFFF, font, x + 20.0f * u,
        y + h - 14.0f * u, scale * 0.62f, scale * 0.62f, 0.0f, dim, 3);
}
#else
void CL_Play_Open(int)
{
}

void CL_Play_StartMultiplayerMenus()
{
}

bool CL_Play_IsOpen()
{
    return false;
}

bool CL_Play_Key(int, int)
{
    return false;
}

void CL_Play_Draw()
{
}
#endif
