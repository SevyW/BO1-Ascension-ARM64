// isteamgameserver.h - the project's own minimal declaration of the Steamworks API the engine calls, used by the native and web builds instead of the SDK.
#pragma once

#include "steam_api.h"

#define BO1_STEAMGAMESERVER_VERSION "SteamGameServer015"

S_API HSteamUser S_CALLTYPE SteamGameServer_GetHSteamUser();
S_API void *S_CALLTYPE SteamInternal_FindOrCreateGameServerInterface(HSteamUser hSteamUser, const char *pszVersion);

class ISteamGameServer
{
public:
    virtual CSteamID GetSteamID() = 0;
    virtual bool BSecure() = 0;
    virtual EUserHasLicenseForAppResult UserHasLicenseForApp(CSteamID steamID, AppId_t appID) = 0;
};

inline ISteamGameServer *SteamGameServer()
{
    return (ISteamGameServer *)SteamInternal_FindOrCreateGameServerInterface(SteamGameServer_GetHSteamUser(), BO1_STEAMGAMESERVER_VERSION);
}

// ----- game server callback payloads -----

struct GSClientApprove_t
{
    enum { k_iCallback = k_iSteamGameServerCallbacks + 1 };
    CSteamID m_SteamID;
    CSteamID m_OwnerSteamID;
};

struct GSClientDeny_t
{
    enum { k_iCallback = k_iSteamGameServerCallbacks + 2 };
    CSteamID m_SteamID;
    EDenyReason m_eDenyReason;
    char m_rgchOptionalText[128];
};

struct GSClientKick_t
{
    enum { k_iCallback = k_iSteamGameServerCallbacks + 3 };
    CSteamID m_SteamID;
    EDenyReason m_eDenyReason;
};

struct GSPolicyResponse_t
{
    enum { k_iCallback = k_iSteamUserCallbacks + 15 };
    uint8 m_bSecure;
};
