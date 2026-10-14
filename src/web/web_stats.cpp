// web_stats.cpp - mod (profile): local player profiles for multiplayer and Combat Training.
//
// Retail keeps a player's stats (rank, unlocks, classes: one DDL buffer, ddl_mp/stats.ddl) on the online service: the
// client fetches the files, sends them to the server, and the server writes them back when a match ends. This build
// has no service (BO1_LIVE / BO1_DW are not defined), so the server's copy started as zeros on every connect and was
// never written anywhere.
//
// Here the server keeps them in files under <fs_homepath>/players/profiles/, one pair per player name:
//   <name>.ct.stats   the Combat Training buffer (client_t::stats, what xblive_basictraining 1 reads and writes)
//   <name>.mp.stats   the multiplayer buffer (client_t::globalStats)
// Loaded when a human player's game state is first sent (SV_SendClientGameState), written where retail uploaded
// (SV_DWWriteClientStats: the script's uploadstats at the end of a match, and when the player leaves). Bots have no
// file. A file of another size (another stats.ddl) is ignored and the player starts over.
//
// The files are the server's: on a dedicated server every player who joins under a name gets that name's profile.
#include <server/server.h>
#include <server_mp/sv_main_mp.h>
#include <qcommon/common.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <live/live_win.h>

#include <stdio.h>
#include <string>
#include <sys/stat.h>

#ifdef BO1_NATIVE
// mod (native): the menus' own stats (live/live_stats.cpp, players/mpstats_offline.bin)
unsigned char *LiveStats_OfflineBuffer(int combatTraining);
void LiveStats_OfflineSave();
#endif

namespace
{
#ifdef BO1_NATIVE
// the host's own player on a listen server
bool IsHost(const client_t *client)
{
    return client->header.netchan.remoteAddress.type == NA_LOOPBACK && !client->bIsTestClient;
}
#endif

bool s_loaded[64];   // per client slot: the profile was read for the current connection

// <fs_homepath>/players/profiles/<name>.<kind>.stats, the name reduced to file-safe characters
std::string ProfilePath(const client_t *client, const char *kind)
{
    std::string path = fs_homepath && fs_homepath->current.string ? fs_homepath->current.string : ".";
    path += "/players";
    mkdir(path.c_str(), 0775);
    path += "/profiles";
    mkdir(path.c_str(), 0775);
    path += '/';
    bool any = false;
    for (const char *c = client->name; *c; ++c)
    {
        const bool safe = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-';
        path += safe ? *c : '_';
        any = true;
    }
    if (!any)
        path += "player";
    path += '.';
    path += kind;
    path += ".stats";
    return path;
}

bool ReadBuffer(const std::string &path, unsigned char *buffer)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    static unsigned char temp[BO1_STATS_SIZE + 1];
    const size_t size = fread(temp, 1, sizeof(temp), file);
    fclose(file);
    if (size != BO1_STATS_SIZE)
    {
        Com_PrintWarning(15, "profile: %s is %u bytes, not %u (another stats.ddl): ignored\n", path.c_str(), (unsigned)size,
            (unsigned)BO1_STATS_SIZE);
        return false;
    }
    memcpy(buffer, temp, BO1_STATS_SIZE);
    return true;
}

void WriteBuffer(const std::string &path, const unsigned char *buffer)
{
    // (through a temporary file: a crash while writing must not leave half a profile)
    const std::string temp = path + ".tmp";
    FILE *file = fopen(temp.c_str(), "wb");
    if (!file)
    {
        Com_PrintWarning(15, "profile: cannot write %s\n", temp.c_str());
        return;
    }
    const size_t written = fwrite(buffer, 1, BO1_STATS_SIZE, file);
    fclose(file);
    if (written == BO1_STATS_SIZE)
        rename(temp.c_str(), path.c_str());
}

int SlotOf(const client_t *client)
{
    const int slot = (int)(client - svs.clients);
    return slot >= 0 && slot < 64 ? slot : -1;
}
}

// SV_SendClientGameState: the player's buffers, from the files or empty
void SV_Profile_Load(client_t *client)
{
    const int slot = SlotOf(client);
    if (slot < 0 || s_loaded[slot])
        return;   // a map change resends the game state: the buffers in memory are newer than the files
    s_loaded[slot] = true;
    memset(client->stats, 0, sizeof(client->stats));
    memset(client->globalStats, 0, sizeof(client->globalStats));
    client->statsValidated = 0;
    if (client->bIsTestClient)
        return;
    const bool ct = ReadBuffer(ProfilePath(client, "ct"), client->stats);
    const bool mp = ReadBuffer(ProfilePath(client, "mp"), client->globalStats);
#ifdef BO1_NATIVE
    // mod (native): the host plays with the stats of the menus (Create a Class, rank, COD points), not with a second
    // copy. Combat Training reads and writes client_t::stats only; the other modes read a stat from client_t::stats
    // unless its DDL permission is 2 and write every stat to client_t::globalStats (SV_GetClientDIntStat,
    // SV_SetClientDIntStat): both start as the menus' buffer of the mode.
    const int combatTraining = xblive_basictraining->current.enabled;
    if (IsHost(client) && LiveStats_OfflineBuffer(combatTraining))
    {
        memcpy(client->stats, LiveStats_OfflineBuffer(combatTraining), BO1_STATS_SIZE);
        if (!combatTraining)
            memcpy(client->globalStats, client->stats, BO1_STATS_SIZE);
        Com_Printf(15, "profile: %s plays with the menus' stats\n", client->name);
    }
#endif
    Com_Printf(15, "profile: %s: Combat Training stats %s, multiplayer stats %s\n", client->name, ct ? "loaded" : "new",
        mp ? "loaded" : "new");
}

// SV_DWWriteClientStats: retail's upload
void SV_Profile_Save(client_t *client)
{
    const int slot = SlotOf(client);
    if (slot < 0 || !s_loaded[slot] || client->bIsTestClient)
        return;
#ifdef BO1_NATIVE
    // mod (native): the host's stats go back to the menus' buffer of the mode and its file (what the match earned
    // shows in the menus); the server's own files are for the other players
    const int combatTraining = xblive_basictraining->current.enabled;
    if (IsHost(client) && LiveStats_OfflineBuffer(combatTraining))
    {
        memcpy(LiveStats_OfflineBuffer(combatTraining), combatTraining ? client->stats : client->globalStats, BO1_STATS_SIZE);
        LiveStats_OfflineSave();
        Com_DPrintf(15, "profile: %s saved\n", client->name);
        return;
    }
#endif
    WriteBuffer(ProfilePath(client, "ct"), client->stats);
    WriteBuffer(ProfilePath(client, "mp"), client->globalStats);
    Com_DPrintf(15, "profile: %s saved\n", client->name);
}

// SV_DropClient (after the save): the slot's next player starts from their own files
void SV_Profile_Forget(client_t *client)
{
    const int slot = SlotOf(client);
    if (slot >= 0)
        s_loaded[slot] = false;
}
