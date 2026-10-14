// steam_gameserver.h - the project's own minimal declaration of the Steamworks API the engine calls, used by the native and web builds instead of the SDK.
#pragma once

#include "steam_api.h"
#include "isteamgameserver.h"

S_API void S_CALLTYPE SteamGameServer_RunCallbacks();
