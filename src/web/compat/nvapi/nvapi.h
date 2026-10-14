// The project's own minimal declarations of the NVAPI the engine uses, for the native and web builds instead of the SDK.
//
// Only what gfx_d3d/{r_init,r_rendertarget,rb_backend}.cpp and web/web_stubs.cpp use. Names, values and field order
// follow the driver library's binary interface. No library is linked: web/web_stubs.cpp implements the functions the
// build reaches (NvAPI_Initialize fails, so dx.nvInitialized stays 0); the INTZ and SLI paths are compiled out
// (BO1_NVAPI_INTZ, BO1_MULTI_GPU).
#pragma once

#include <d3d9.h>

typedef unsigned char NvU8;
typedef unsigned long NvU32;

typedef enum
{
    NVAPI_OK = 0,
    NVAPI_LIBRARY_NOT_FOUND = -2,
} NvAPI_Status;

// opaque driver handles
struct NvDisplayHandle__
{
    int unused;
};
typedef struct NvDisplayHandle__ *NvDisplayHandle;

struct NVDX_ObjectHandle__
{
    int unused;
};
typedef struct NVDX_ObjectHandle__ *NVDX_ObjectHandle;

typedef void *StereoHandle;

#define NVAPI_SHORT_STRING_MAX 64
typedef char NvAPI_ShortString[NVAPI_SHORT_STRING_MAX];

// The version field of these structs is sizeof(struct) | (struct version << 16) (32-bit Windows sizes).
typedef struct
{
    NvU32 version;
    NvU32 drvVersion;
    NvU32 bldChangeListNum;
    NvAPI_ShortString szBuildBranchString;
    NvAPI_ShortString szAdapterString;
} NV_DISPLAY_DRIVER_VERSION;

typedef struct
{
    NvU32 version;
    NvU32 maxNumAFRGroups;
    NvU32 numAFRGroups;
    NvU32 currentAFRIndex;
    NvU32 nextFrameAFRIndex;
    NvU32 previousFrameAFRIndex;
    NvU32 bIsCurAFRGroupNew;
} NV_GET_CURRENT_SLI_STATE;

extern "C" {

NvAPI_Status __cdecl NvAPI_Initialize();
NvAPI_Status __cdecl NvAPI_GetDisplayDriverVersion(NvDisplayHandle hNvDisplay, NV_DISPLAY_DRIVER_VERSION *pVersion);

NvAPI_Status __cdecl NvAPI_Stereo_IsEnabled(NvU8 *pIsStereoEnabled);
NvAPI_Status __cdecl NvAPI_Stereo_CreateHandleFromIUnknown(IUnknown *pDevice, StereoHandle *pStereoHandle);
NvAPI_Status __cdecl NvAPI_Stereo_DestroyHandle(StereoHandle stereoHandle);
NvAPI_Status __cdecl NvAPI_Stereo_IsActivated(StereoHandle stereoHandle, NvU8 *pIsStereoOn);
NvAPI_Status __cdecl NvAPI_Stereo_SetConvergence(StereoHandle stereoHandle, float newConvergence);

NvAPI_Status __cdecl NvAPI_D3D_GetCurrentSLIState(IUnknown *pDevice, NV_GET_CURRENT_SLI_STATE *pSliState);
NvAPI_Status __cdecl NvAPI_D3D9_GetTextureHandle(IDirect3DTexture9 *pTexture, NVDX_ObjectHandle *pHandle);
NvAPI_Status __cdecl NvAPI_D3D9_GetCurrentZBufferHandle(IDirect3DDevice9 *pDev, NVDX_ObjectHandle *pHandle);
NvAPI_Status __cdecl NvAPI_D3D9_StretchRect(IDirect3DDevice9 *pDev, NVDX_ObjectHandle hSrcObj, const RECT *pSourceRect,
                                            NVDX_ObjectHandle hDstObj, const RECT *pDestRect,
                                            D3DTEXTUREFILTERTYPE Filter);

} // extern "C"
