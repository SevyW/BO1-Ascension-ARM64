// steamclientpublic.h - the project's own minimal declaration of the Steamworks API the engine calls, used by the native and web builds instead of the SDK.
#pragma once

#include <stddef.h>
#include <stdint.h>

// the same types as universal/q_shared.h (identical redeclarations)
typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;

#define S_API extern "C"
#define S_CALLTYPE __cdecl

typedef uint32 AppId_t;
typedef uint32 AccountID_t;
typedef uint64 SteamAPICall_t;
typedef uint32 HAuthTicket;
typedef int32 HSteamUser;
typedef int32 HSteamPipe;

const SteamAPICall_t k_uAPICallInvalid = 0;
const HAuthTicket k_HAuthTicketInvalid = 0;

enum EResult
{
    k_EResultNone = 0,
    k_EResultOK = 1,
    k_EResultFail = 2,
    k_EResultNoConnection = 3,
    k_EResultLimitExceeded = 25,
    k_EResultDuplicateRequest = 29,
};

enum EDenyReason
{
    k_EDenyInvalid = 0,
    k_EDenyInvalidVersion = 1,
    k_EDenyGeneric = 2,
    k_EDenyNotLoggedOn = 3,
    k_EDenyNoLicense = 4,
    k_EDenyCheater = 5,
    k_EDenyLoggedInElseWhere = 6,
    k_EDenyUnknownText = 7,
    k_EDenyIncompatibleAnticheat = 8,
    k_EDenyMemoryCorruption = 9,
    k_EDenyIncompatibleSoftware = 10,
    k_EDenySteamConnectionLost = 11,
    k_EDenySteamConnectionError = 12,
    k_EDenySteamResponseTimedOut = 13,
    k_EDenySteamValidationStalled = 14,
    k_EDenySteamOwnerLeftGuestUser = 15,
};

enum EBeginAuthSessionResult
{
    k_EBeginAuthSessionResultOK = 0,
};

enum EAuthSessionResponse
{
    k_EAuthSessionResponseOK = 0,
};

enum EUserHasLicenseForAppResult
{
    k_EUserHasLicenseResultHasLicense = 0,
    k_EUserHasLicenseResultDoesNotHaveLicense = 1,
    k_EUserHasLicenseResultNoAuth = 2,
};

// A 64-bit Steam ID: account ID in bits 0-31, instance in 32-51, account type in 52-55, universe in 56-63.
class CSteamID
{
public:
    CSteamID() : m_steamid(0) {}
    CSteamID(uint64 ulSteamID) : m_steamid(ulSteamID) {}

    void SetFromUint64(uint64 ulSteamID) { m_steamid = ulSteamID; }
    uint64 ConvertToUint64() const { return m_steamid; }

    AccountID_t GetAccountID() const { return (AccountID_t)(m_steamid & 0xFFFFFFFFull); }
    uint32 GetUnAccountInstance() const { return (uint32)((m_steamid >> 32) & 0xFFFFF); }
    int GetEAccountType() const { return (int)((m_steamid >> 52) & 0xF); }
    int GetEUniverse() const { return (int)((m_steamid >> 56) & 0xFF); }

    bool IsValid() const
    {
        const int type = GetEAccountType();
        const int universe = GetEUniverse();
        if (type <= 0 || type >= 11)            // invalid .. max account type
            return false;
        if (universe <= 0 || universe >= 5)     // invalid .. max universe
            return false;
        if (type == 1)                          // individual
            return GetAccountID() != 0 && GetUnAccountInstance() <= 4;
        if (type == 7)                          // clan
            return GetAccountID() != 0 && GetUnAccountInstance() == 0;
        if (type == 3)                          // game server
            return GetAccountID() != 0;
        return true;
    }

    bool operator==(const CSteamID &other) const { return m_steamid == other.m_steamid; }
    bool operator!=(const CSteamID &other) const { return m_steamid != other.m_steamid; }

private:
    uint64 m_steamid;
};
