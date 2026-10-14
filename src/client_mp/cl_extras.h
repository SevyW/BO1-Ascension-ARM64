#pragma once

// mod (native menus): the "Extras" settings screen of the native builds (cl_extras.cpp); nothing in the Windows build
void CL_Extras_Open();
bool CL_Extras_IsOpen();
bool CL_Extras_Key(int localClientNum, int key);   // a key going down; true when the screen took it
bool CL_Extras_Char(int character);
void CL_Extras_Draw();
