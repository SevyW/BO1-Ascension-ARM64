#pragma once

// mod (coop): the "join a game by address" screen (cl_joinmenu.cpp)
void CL_JoinMenu_Open();
bool CL_JoinMenu_IsOpen();
bool CL_JoinMenu_Key(int localClientNum, int key);   // a key going down; true when the screen took it
bool CL_JoinMenu_Char(int character);
void CL_JoinMenu_Draw();
struct netadr_t;
// "bo1here": a host found on the local network. From whichever side of this program reads the socket: the client,
// or its own server while one runs (the front end is a local server).
void CL_JoinMenu_Found(netadr_t from, const char *map, const char *players, const char *maxPlayers);
