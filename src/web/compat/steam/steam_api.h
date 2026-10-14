// steam_api.h - the project's own minimal declaration of the Steamworks API the engine calls, used by the native and web builds instead of the SDK.
#pragma once

#include "steamclientpublic.h"

// ----- the flat C API (src/web/web_steamapi.cpp implements it as "Steam is not running") -----

enum ESteamAPIInitResult
{
    k_ESteamAPIInitResult_OK = 0,
    k_ESteamAPIInitResult_FailedGeneric = 1,
    k_ESteamAPIInitResult_NoSteamClient = 2,
    k_ESteamAPIInitResult_VersionMismatch = 3,
};

const int k_cchMaxSteamErrMsg = 1024;
typedef char SteamErrMsg[k_cchMaxSteamErrMsg];

extern "C" typedef void(S_CALLTYPE *SteamAPIWarningMessageHook_t)(int, const char *);

class CCallbackBase;

S_API ESteamAPIInitResult S_CALLTYPE SteamInternal_SteamAPI_Init(const char *pszInternalCheckInterfaceVersions, SteamErrMsg *pOutErrMsg);
S_API void S_CALLTYPE SteamAPI_Shutdown();
S_API void S_CALLTYPE SteamAPI_RunCallbacks();
S_API void S_CALLTYPE SteamAPI_RegisterCallback(CCallbackBase *pCallback, int iCallback);
S_API void S_CALLTYPE SteamAPI_UnregisterCallback(CCallbackBase *pCallback);
S_API void S_CALLTYPE SteamAPI_RegisterCallResult(CCallbackBase *pCallback, SteamAPICall_t hAPICall);
S_API void S_CALLTYPE SteamAPI_UnregisterCallResult(CCallbackBase *pCallback, SteamAPICall_t hAPICall);
S_API HSteamUser S_CALLTYPE SteamAPI_GetHSteamUser();
S_API void *S_CALLTYPE SteamInternal_ContextInit(void *pContextInitData);
S_API void *S_CALLTYPE SteamInternal_CreateInterface(const char *ver);
S_API void *S_CALLTYPE SteamInternal_FindOrCreateUserInterface(HSteamUser hSteamUser, const char *pszVersion);

#define BO1_STEAMCLIENT_VERSION "SteamClient023"
#define BO1_STEAMUSER_VERSION "SteamUser023"
#define BO1_STEAMFRIENDS_VERSION "SteamFriends018"
#define BO1_STEAMUTILS_VERSION "SteamUtils010"
#define BO1_STEAMNETWORKING_VERSION "SteamNetworking006"
#define BO1_STEAMUSERSTATS_VERSION "STEAMUSERSTATS_INTERFACE_VERSION013"

inline ESteamAPIInitResult SteamAPI_InitEx(SteamErrMsg *pOutErrMsg)
{
    return SteamInternal_SteamAPI_Init(BO1_STEAMCLIENT_VERSION "\0" BO1_STEAMUSER_VERSION "\0" BO1_STEAMFRIENDS_VERSION "\0"
        BO1_STEAMUTILS_VERSION "\0" BO1_STEAMNETWORKING_VERSION "\0" BO1_STEAMUSERSTATS_VERSION "\0", pOutErrMsg);
}

// ----- callbacks and call results -----

enum { k_iSteamUserCallbacks = 100 };
enum { k_iSteamGameServerCallbacks = 200 };
enum { k_iSteamUserStatsCallbacks = 1100 };
enum { k_iSteamNetworkingCallbacks = 1200 };

class CCallbackBase
{
public:
    CCallbackBase() : m_nCallbackFlags(0), m_iCallback(0) {}
    virtual void Run(void *pvParam) = 0;
    virtual void Run(void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    int GetICallback() { return m_iCallback; }

protected:
    enum { k_ECallbackFlagsRegistered = 0x01, k_ECallbackFlagsGameServer = 0x02 };
    uint8 m_nCallbackFlags;
    int m_iCallback;
};

// a member function of T that receives every P the client (or the game server) posts
template <class T, class P, bool bGameServer = false>
class CCallback : public CCallbackBase
{
public:
    typedef void (T::*func_t)(P *);

    CCallback(T *pObj, func_t func) : m_pObj(nullptr), m_Func(nullptr)
    {
        if (bGameServer)
            m_nCallbackFlags |= k_ECallbackFlagsGameServer;
        Register(pObj, func);
    }
    ~CCallback() { Unregister(); }

    void Register(T *pObj, func_t func)
    {
        if (!pObj || !func)
            return;
        if (m_nCallbackFlags & k_ECallbackFlagsRegistered)
            Unregister();
        m_pObj = pObj;
        m_Func = func;
        m_iCallback = P::k_iCallback;
        SteamAPI_RegisterCallback(this, P::k_iCallback);
    }
    void Unregister() { SteamAPI_UnregisterCallback(this); }

protected:
    void Run(void *pvParam) override { (m_pObj->*m_Func)((P *)pvParam); }
    void Run(void *pvParam, bool, SteamAPICall_t) override { (m_pObj->*m_Func)((P *)pvParam); }
    int GetCallbackSizeBytes() override { return (int)sizeof(P); }

    T *m_pObj;
    func_t m_Func;
};

// a member function of T that receives the P answering one asynchronous call (SteamAPICall_t)
template <class T, class P>
class CCallResult : private CCallbackBase
{
public:
    typedef void (T::*func_t)(P *, bool);

    CCallResult() : m_hAPICall(k_uAPICallInvalid), m_pObj(nullptr), m_Func(nullptr) { m_iCallback = P::k_iCallback; }
    ~CCallResult() { Cancel(); }

    void Set(SteamAPICall_t hAPICall, T *pObj, func_t func)
    {
        if (m_hAPICall != k_uAPICallInvalid)
            SteamAPI_UnregisterCallResult(this, m_hAPICall);
        m_hAPICall = hAPICall;
        m_pObj = pObj;
        m_Func = func;
        if (hAPICall != k_uAPICallInvalid)
            SteamAPI_RegisterCallResult(this, hAPICall);
    }
    bool IsActive() const { return m_hAPICall != k_uAPICallInvalid; }
    void Cancel()
    {
        if (m_hAPICall != k_uAPICallInvalid)
        {
            SteamAPI_UnregisterCallResult(this, m_hAPICall);
            m_hAPICall = k_uAPICallInvalid;
        }
    }

private:
    void Run(void *pvParam) override
    {
        m_hAPICall = k_uAPICallInvalid;
        (m_pObj->*m_Func)((P *)pvParam, false);
    }
    void Run(void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) override
    {
        if (hSteamAPICall == m_hAPICall)
        {
            m_hAPICall = k_uAPICallInvalid;
            (m_pObj->*m_Func)((P *)pvParam, bIOFailure);
        }
    }
    int GetCallbackSizeBytes() override { return (int)sizeof(P); }

    SteamAPICall_t m_hAPICall;
    T *m_pObj;
    func_t m_Func;
};

#define STEAM_CALLBACK(thisclass, func, param, var) \
    CCallback<thisclass, param, false> var;         \
    void func(param *pParam)

// ----- callback payloads -----

struct SteamServersConnected_t
{
    enum { k_iCallback = k_iSteamUserCallbacks + 1 };
};

struct SteamServersDisconnected_t
{
    enum { k_iCallback = k_iSteamUserCallbacks + 3 };
    EResult m_eResult;
};

struct ValidateAuthTicketResponse_t
{
    enum { k_iCallback = k_iSteamUserCallbacks + 43 };
    CSteamID m_SteamID;
    EAuthSessionResponse m_eAuthSessionResponse;
    CSteamID m_OwnerSteamID;
};

struct EncryptedAppTicketResponse_t
{
    enum { k_iCallback = k_iSteamUserCallbacks + 54 };
    EResult m_eResult;
};

struct P2PSessionRequest_t
{
    enum { k_iCallback = k_iSteamNetworkingCallbacks + 2 };
    CSteamID m_steamIDRemote;
};

struct UserStatsReceived_t
{
    enum { k_iCallback = k_iSteamUserStatsCallbacks + 1 };
    uint64 m_nGameID;
    EResult m_eResult;
    CSteamID m_steamIDUser;
};

struct UserStatsStored_t
{
    enum { k_iCallback = k_iSteamUserStatsCallbacks + 2 };
    uint64 m_nGameID;
    EResult m_eResult;
};

struct UserAchievementStored_t
{
    enum { k_iCallback = k_iSteamUserStatsCallbacks + 3 };
    uint64 m_nGameID;
    bool m_bGroupAchievement;
    char m_rgchAchievementName[128];
    uint32 m_nCurProgress;
    uint32 m_nMaxProgress;
};

// ----- interfaces (only the methods the engine calls; the accessors return null without the Steam client) -----

enum EPersonaState
{
    k_EPersonaStateOffline = 0,
    k_EPersonaStateOnline = 1,
    k_EPersonaStateBusy = 2,
    k_EPersonaStateAway = 3,
};

enum EFriendRelationship
{
    k_EFriendRelationshipNone = 0,
    k_EFriendRelationshipFriend = 3,
};

enum EP2PSend
{
    k_EP2PSendUnreliable = 0,
    k_EP2PSendUnreliableNoDelay = 1,
    k_EP2PSendReliable = 2,
    k_EP2PSendReliableWithBuffering = 3,
};

// an IPv4 address and port as the auth ticket's intended recipient (IPv4-mapped IPv6, host byte order port)
struct SteamNetworkingIdentity
{
    int m_eType;
    int m_cbSize;
    uint8 m_ipv6[16];
    uint16 m_port;

    void SetIPv4Addr(uint32 nIPv4, uint16 nPort)
    {
        for (int i = 0; i < 10; ++i)
            m_ipv6[i] = 0;
        m_ipv6[10] = 0xFF;
        m_ipv6[11] = 0xFF;
        m_ipv6[12] = (uint8)(nIPv4 >> 24);
        m_ipv6[13] = (uint8)(nIPv4 >> 16);
        m_ipv6[14] = (uint8)(nIPv4 >> 8);
        m_ipv6[15] = (uint8)nIPv4;
        m_port = nPort;
        m_eType = 1;    // IP address
        m_cbSize = 18;  // address + port
    }
};

class ISteamClient
{
public:
    virtual void SetWarningMessageHook(SteamAPIWarningMessageHook_t pFunction) = 0;
};

class ISteamUser
{
public:
    virtual bool BLoggedOn() = 0;
    virtual CSteamID GetSteamID() = 0;
    virtual HAuthTicket GetAuthSessionTicket(void *pTicket, int cbMaxTicket, uint32 *pcbTicket, const SteamNetworkingIdentity *pSnid) = 0;
    virtual EBeginAuthSessionResult BeginAuthSession(const void *pAuthTicket, int cbAuthTicket, CSteamID steamID) = 0;
    virtual void EndAuthSession(CSteamID steamID) = 0;
    virtual void CancelAuthTicket(HAuthTicket hAuthTicket) = 0;
    virtual SteamAPICall_t RequestEncryptedAppTicket(void *pDataToInclude, int cbDataToInclude) = 0;
    virtual bool GetEncryptedAppTicket(void *pTicket, int cbMaxTicket, uint32 *pcbTicket) = 0;
};

class ISteamFriends
{
public:
    virtual const char *GetPersonaName() = 0;
    virtual int GetFriendCount(int iFriendFlags) = 0;
    virtual CSteamID GetFriendByIndex(int iFriend, int iFriendFlags) = 0;
    virtual EFriendRelationship GetFriendRelationship(CSteamID steamIDFriend) = 0;
    virtual EPersonaState GetFriendPersonaState(CSteamID steamIDFriend) = 0;
    virtual const char *GetFriendPersonaName(CSteamID steamIDFriend) = 0;
    virtual void ActivateGameOverlayToUser(const char *pchDialog, CSteamID steamID) = 0;
};

class ISteamUtils
{
public:
    virtual const char *GetIPCountry() = 0;
    virtual AppId_t GetAppID() = 0;
};

class ISteamNetworking
{
public:
    virtual bool SendP2PPacket(CSteamID steamIDRemote, const void *pubData, uint32 cubData, EP2PSend eP2PSendType, int nChannel) = 0;
    virtual bool IsP2PPacketAvailable(uint32 *pcubMsgSize, int nChannel) = 0;
    virtual bool AcceptP2PSessionWithUser(CSteamID steamIDRemote) = 0;
};

class ISteamUserStats
{
public:
    virtual bool GetAchievement(const char *pchName, bool *pbAchieved) = 0;
    virtual bool SetAchievement(const char *pchName) = 0;
    virtual bool StoreStats() = 0;
    virtual const char *GetAchievementDisplayAttribute(const char *pchName, const char *pchKey) = 0;
    virtual bool ResetAllStats(bool bAchievementsToo) = 0;
};

inline ISteamClient *SteamClient()
{
    return (ISteamClient *)SteamInternal_CreateInterface(BO1_STEAMCLIENT_VERSION);
}

inline ISteamUser *SteamUser()
{
    return (ISteamUser *)SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), BO1_STEAMUSER_VERSION);
}

inline ISteamFriends *SteamFriends()
{
    return (ISteamFriends *)SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), BO1_STEAMFRIENDS_VERSION);
}

inline ISteamUtils *SteamUtils()
{
    return (ISteamUtils *)SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), BO1_STEAMUTILS_VERSION);
}

inline ISteamNetworking *SteamNetworking()
{
    return (ISteamNetworking *)SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), BO1_STEAMNETWORKING_VERSION);
}

inline ISteamUserStats *SteamUserStats()
{
    return (ISteamUserStats *)SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), BO1_STEAMUSERSTATS_VERSION);
}
