// cl_joinmenu.cpp - mod (coop): "join a game by address", drawn by the engine over whatever is on screen.
//
// The menus come from the retail fastfiles and have no entry for joining a host of this build by address; the only
// way was the console (connect <ip>:<port>) or the command line. This is a small screen of its own: an address
// line, an on-screen keypad (a phone has no keyboard, a controller cannot type), and the last addresses used.
//
// Open it with F4, with the controller's Back button while no game is being played (front end, main menu), or with
// the console command "joinmenu". D-pad / arrow keys move, A / Enter presses the highlighted key, X / Backspace
// deletes, Start joins, B / Escape closes; a keyboard can also type the address. Joining sets systemlink 1 and runs
// "connect <address>", with port 28960 when none was given. The last four addresses are kept in the archived dvar
// bo1_join_recent (default "", space separated).
//
// Games on the local network: while the screen is open it broadcasts "bo1find" to port 28960 every 3 s; hosts of
// this build that take players by address answer "bo1here" (server_mp/sv_main_mp.cpp) and are listed under the
// recent addresses with their map and player count. Choosing one puts its address on the line.
#include "cl_joinmenu.h"
#include "cl_main_mp.h"
#include <client/client.h>
#include <client/screen_placement.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <qcommon/net_chan_mp.h>
#include <win32/win_shared.h>
#include <ui/keycodes.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

namespace
{
const int kRecentMax = 4;
const int kKeypadRows = 4;   // rows of three: 1 2 3 / 4 5 6 / 7 8 9 / . 0 :
const char kKeypad[kKeypadRows * 3 + 1] = "123456789.0:";
const char *const kActions[3] = { "DELETE", "JOIN", "CLOSE" };

bool s_open;
char s_address[64];
int s_row;      // 0..3 keypad, 4 actions, 5.. recent addresses
int s_column;   // 0..2 (0 for a recent address)
char s_recent[kRecentMax][64];
int s_recentCount;
const dvar_t *bo1_join_recent;
cmd_function_s s_joinmenuCmd;
bool s_registered;
const int kFoundMax = 4;
char s_found[kFoundMax][64];       // address
char s_foundInfo[kFoundMax][64];   // map and players
int s_foundCount;
unsigned int s_lastFind;

void Find()
{
    netadr_t to;
    memset(&to, 0, sizeof(to));
    to.type = NA_BROADCAST;
    to.port = (unsigned short)(((28960 & 0xFF) << 8) | (28960 >> 8));   // network byte order
    NET_OutOfBandPrint(NS_CLIENT1, to, "bo1find");
    s_lastFind = Sys_Milliseconds();
}

void LoadRecent()
{
    s_recentCount = 0;
    if (!bo1_join_recent)
        return;
    const char *p = bo1_join_recent->current.string;
    while (*p && s_recentCount < kRecentMax)
    {
        while (*p == ' ')
            ++p;
        int n = 0;
        while (*p && *p != ' ')
        {
            if (n < 63)
                s_recent[s_recentCount][n++] = *p;
            ++p;
        }
        s_recent[s_recentCount][n] = 0;
        if (n)
            ++s_recentCount;
    }
}

void Remember(const char *address)
{
    char list[kRecentMax * 64 + 8];
    I_strncpyz(list, address, sizeof(list));
    int kept = 1;
    for (int i = 0; i < s_recentCount && kept < kRecentMax; ++i)
    {
        if (!I_stricmp(s_recent[i], address))
            continue;
        I_strncat(list, sizeof(list), " ");
        I_strncat(list, sizeof(list), s_recent[i]);
        ++kept;
    }
    Dvar_SetString((dvar_s *)bo1_join_recent, list);
    LoadRecent();
}

void Open()
{
    LoadRecent();
    s_foundCount = 0;
    Find();
    if (!s_address[0] && s_recentCount)
        I_strncpyz(s_address, s_recent[0], sizeof(s_address));
    s_row = 0;
    s_column = 0;
    s_open = true;
}

void OpenCommand()
{
    Open();
}

void Type(char c)
{
    const size_t length = strlen(s_address);
    if (length + 1 < sizeof(s_address))
    {
        s_address[length] = c;
        s_address[length + 1] = 0;
    }
}

void Delete()
{
    const size_t length = strlen(s_address);
    if (length)
        s_address[length - 1] = 0;
}

void Join()
{
    if (!s_address[0])
        return;
    char address[80];
    I_strncpyz(address, s_address, sizeof(address));
    if (!strchr(address, ':'))
        I_strncat(address, sizeof(address), ":28960");   // the port the hosts of this build use (docs/multiplayer.md)
    Remember(s_address);
    s_open = false;
    Com_Printf(0, "coop: joining %s\n", address);
    Cbuf_AddText(0, va("set systemlink 1\nconnect %s\n", address));
}

void Press()
{
    if (s_row < kKeypadRows)
        Type(kKeypad[s_row * 3 + s_column]);
    else if (s_row == kKeypadRows)
    {
        if (s_column == 0)
            Delete();
        else if (s_column == 1)
            Join();
        else
            s_open = false;
    }
    else if (s_row - kKeypadRows - 1 < s_recentCount)
    {
        I_strncpyz(s_address, s_recent[s_row - kKeypadRows - 1], sizeof(s_address));
        s_row = kKeypadRows;
        s_column = 1;   // JOIN
    }
    else if (s_row - kKeypadRows - 1 - s_recentCount < s_foundCount)
    {
        I_strncpyz(s_address, s_found[s_row - kKeypadRows - 1 - s_recentCount], sizeof(s_address));
        s_row = kKeypadRows;
        s_column = 1;   // JOIN
    }
}

void Move(int rows, int columns)
{
    const int lastRow = kKeypadRows + s_recentCount + s_foundCount;
    s_row += rows;
    if (s_row < 0)
        s_row = lastRow;
    if (s_row > lastRow)
        s_row = 0;
    if (s_row > kKeypadRows)
        s_column = 0;
    else
        s_column = (s_column + columns + 3) % 3;
}

void Box(float x, float y, float w, float h, const float *color)
{
    R_AddCmdDrawStretchPic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color, cls.whiteMaterial);
}

void Text(const char *text, float x, float y, float scale, const float *color)
{
    R_AddCmdDrawText((char *)text, 0x7FFFFFFF, cls.consoleFont, x, y, scale, scale, 0.0f, color, 3);
}
}

// the retail menu's Zombies > Online entry (ui/ui_shared.cpp Item_Action) and the Extras screen
void CL_JoinMenu_Open()
{
    Open();
}

bool CL_JoinMenu_IsOpen()
{
    return s_open;
}

// CL_KeyEvent / CL_GamepadButtonEvent, a key going down: true when this screen took it
bool CL_JoinMenu_Key(int localClientNum, int key)
{
    if (!s_open)
    {
        // Back opens it only while no game is being played (in a game it is the scoreboard)
        const connstate_t state = CL_GetLocalClientConnectionState(localClientNum);
        const bool playing = state == CA_ACTIVE && !Com_IsSPFrontEndRunning();
        if (key == K_F4 || (key == K_BUTTON_BACK && !playing))
        {
            Open();
            return true;
        }
        return false;
    }
    switch (key)
    {
    case K_DPAD_UP:
    case K_UPARROW:
        Move(-1, 0);
        break;
    case K_DPAD_DOWN:
    case K_DOWNARROW:
        Move(1, 0);
        break;
    case K_DPAD_LEFT:
    case K_LEFTARROW:
        Move(0, -1);
        break;
    case K_DPAD_RIGHT:
    case K_RIGHTARROW:
        Move(0, 1);
        break;
    case K_BUTTON_A:
        Press();
        break;
    case K_BUTTON_X:
    case K_BACKSPACE:
        Delete();
        break;
    case K_BUTTON_START:
    case K_ENTER:
    case K_KP_ENTER:
        Join();
        break;
    case K_BUTTON_B:
    case K_BUTTON_BACK:
    case K_ESCAPE:
    case K_F4:
        s_open = false;
        break;
    default:
        break;
    }
    return true;   // nothing reaches the game or its menus while this is open
}

// CL_CharEvent: a keyboard types the address directly
bool CL_JoinMenu_Char(int character)
{
    if (!s_open)
        return false;
    if ((character >= '0' && character <= '9') || character == '.' || character == ':' || character == '-'
        || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z'))
        Type((char)character);
    return true;
}

// A host's answer to Find (CL_ConnectionlessPacket, SV_ConnectionlessPacket)
void CL_JoinMenu_Found(netadr_t from, const char *map, const char *players, const char *maxPlayers)
{
    if (!s_open)
        return;
    const char *address = NET_AdrToString(from);
    int slot = -1;
    for (int i = 0; i < s_foundCount; ++i)
        if (!I_stricmp(s_found[i], address))
            slot = i;
    if (slot < 0 && s_foundCount < kFoundMax)
        slot = s_foundCount++;
    if (slot >= 0)
    {
        I_strncpyz(s_found[slot], address, sizeof(s_found[slot]));
        Com_sprintf(s_foundInfo[slot], sizeof(s_foundInfo[slot]), "%s  %s/%s", map, players, maxPlayers);
    }
}

// CL_DrawScreen, every frame
void CL_JoinMenu_Draw()
{
    if (!s_registered)
    {
        s_registered = true;
        bo1_join_recent = _Dvar_RegisterString("bo1_join_recent", (char *)"", 1u, "mod (coop): the last addresses joined from the join screen");
        Cmd_AddCommandInternal("joinmenu", OpenCommand, &s_joinmenuCmd);
    }
    if (!s_open || !cls.consoleFont || !cls.whiteMaterial)
        return;
    if (Sys_Milliseconds() - s_lastFind > 3000)
        Find();

    static const float shade[4] = { 0.0f, 0.0f, 0.0f, 0.82f };
    static const float panel[4] = { 0.10f, 0.11f, 0.12f, 0.96f };
    static const float cell[4] = { 0.20f, 0.21f, 0.23f, 1.0f };
    static const float selected[4] = { 0.85f, 0.55f, 0.10f, 1.0f };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float dim[4] = { 0.70f, 0.70f, 0.70f, 1.0f };

    const float screenW = scrPlaceFull.realViewportSize[0], screenH = scrPlaceFull.realViewportSize[1];
    if (screenW <= 0.0f || screenH <= 0.0f)
        return;
    const float u = screenH / 720.0f;   // the layout is for 720 lines
    const float fontHeight = (float)R_TextHeight(cls.consoleFont);
    const float scale = fontHeight > 0.0f ? 22.0f * u / fontHeight : 1.0f;
    const float line = 22.0f * u;

    const float w = 420.0f * u;
    const float h = (300.0f + 34.0f * (s_recentCount + s_foundCount)) * u + 3.0f * line;
    const float x = (screenW - w) * 0.5f, y = (screenH - h) * 0.5f;
    Box(0.0f, 0.0f, screenW, screenH, shade);
    Box(x, y, w, h, panel);

    float cy = y + 16.0f * u + line;
    Text("JOIN A GAME BY ADDRESS", x + 20.0f * u, cy, scale, white);
    cy += 14.0f * u;
    Box(x + 20.0f * u, cy, w - 40.0f * u, 38.0f * u, cell);
    Text(va("%s_", s_address), x + 30.0f * u, cy + 29.0f * u, scale, white);
    cy += 50.0f * u;

    const float cellW = (w - 40.0f * u - 16.0f * u) / 3.0f, cellH = 36.0f * u;
    for (int row = 0; row <= kKeypadRows; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const float cx = x + 20.0f * u + column * (cellW + 8.0f * u);
            const bool isSelected = row == s_row && column == s_column;
            Box(cx, cy, cellW, cellH, isSelected ? selected : cell);
            char label[2] = { row < kKeypadRows ? kKeypad[row * 3 + column] : (char)0, 0 };
            Text(row < kKeypadRows ? label : kActions[column], cx + 12.0f * u, cy + 27.0f * u, scale, white);
        }
        cy += cellH + 6.0f * u;
    }
    for (int i = 0; i < s_recentCount; ++i)
    {
        const bool isSelected = s_row == kKeypadRows + 1 + i;
        Box(x + 20.0f * u, cy, w - 40.0f * u, 28.0f * u, isSelected ? selected : cell);
        Text(s_recent[i], x + 30.0f * u, cy + 22.0f * u, scale * 0.85f, white);
        cy += 34.0f * u;
    }
    for (int i = 0; i < s_foundCount; ++i)
    {
        const bool isSelected = s_row == kKeypadRows + 1 + s_recentCount + i;
        Box(x + 20.0f * u, cy, w - 40.0f * u, 28.0f * u, isSelected ? selected : cell);
        Text(va("LAN  %s  %s", s_found[i], s_foundInfo[i]), x + 30.0f * u, cy + 22.0f * u, scale * 0.7f, white);
        cy += 34.0f * u;
    }
    cy += line;
    Text("D-pad: move   A: press   X: delete   Start: join   B: close", x + 20.0f * u, cy, scale * 0.6f, dim);
}
