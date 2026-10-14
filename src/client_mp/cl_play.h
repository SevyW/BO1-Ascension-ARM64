#pragma once

// mod (native menus): the "Play" screen of the native builds (cl_play.cpp); nothing in the Windows build
void CL_Play_Open(int family);   // 0 Zombies, 1 Multiplayer, -1 the mode the program runs in
bool CL_Play_IsOpen();
bool CL_Play_Key(int localClientNum, int key);   // a key going down; true when the screen took it
void CL_Play_Draw();
