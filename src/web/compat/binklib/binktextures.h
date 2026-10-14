// The project's own minimal declarations of the Bink API the engine uses, for the native and web builds instead of the SDK.
//
// Only what gfx_d3d/r_cinematic.{h,cpp} and web/web_cinematic.cpp use. Types, field order and the 32-bit Windows
// sizes (U32, S32, UINTa = 32 bits) follow the library's binary interface, so CinematicGlob keeps the layout it had with
// the SDK headers. No library is linked: web/web_cinematic.cpp implements these functions (BinkOpen fails, no video
// opens).
#pragma once

#include <d3d9.h>

typedef unsigned int U32;
typedef signed int S32;
typedef unsigned int UINTa;

#define BINKMAXFRAMEBUFFERS 2

// Bink handle. Owned by the library; the engine reads only these leading fields.
typedef struct BINK
{
    U32 Width;
    U32 Height;
    U32 Frames;       // frame count
    U32 FrameNum;     // next frame to show, 1-based
    U32 LastFrameNum;
    U32 FrameRate;
    U32 FrameRateDiv;
} BINK;
typedef BINK *HBINK;

// one decoded plane (Y, cR, cB or alpha) of one frame buffer
typedef struct BINKPLANE
{
    S32 Allocate;
    void *Buffer;
    U32 BufferPitch;
} BINKPLANE;

typedef struct BINKFRAMEPLANESET
{
    BINKPLANE YPlane;
    BINKPLANE cRPlane;
    BINKPLANE cBPlane;
    BINKPLANE APlane;
} BINKFRAMEPLANESET;

typedef struct BINKFRAMEBUFFERS
{
    S32 TotalFrames;
    U32 YABufferWidth;
    U32 YABufferHeight;
    U32 cRcBBufferWidth;
    U32 cRcBBufferHeight;
    U32 FrameNum;     // index of the buffer holding the current frame
    BINKFRAMEPLANESET Frames[BINKMAXFRAMEBUFFERS];
} BINKFRAMEBUFFERS;

// playback statistics (BinkGetRealtime)
typedef struct BINKREALTIME
{
    U32 FrameNum;
    U32 FrameRate;
    U32 FrameRateDiv;
    U32 Frames;
    U32 FramesTime;
    U32 FramesVideoDecompTime;
    U32 FramesAudioDecompTime;
    U32 FramesReadTime;
    U32 FramesIdleReadTime;
    U32 FramesThreadReadTime;
    U32 FramesBlitTime;
    U32 ReadBufferSize;
    U32 ReadBufferUsed;
    U32 FramesDataRate;
} BINKREALTIME;

// D3D9 textures of one frame (the Windows variant)
typedef struct BINKFRAMETEXTURES
{
    U32 Ysize;
    U32 cRsize;
    U32 cBsize;
    U32 Asize;
    LPDIRECT3DTEXTURE9 Ytexture;
    LPDIRECT3DTEXTURE9 cRtexture;
    LPDIRECT3DTEXTURE9 cBtexture;
    LPDIRECT3DTEXTURE9 Atexture;
} BINKFRAMETEXTURES;

typedef struct BINKTEXTURESET
{
    BINKFRAMEBUFFERS bink_buffers;
    BINKFRAMETEXTURES textures[BINKMAXFRAMEBUFFERS];
    BINKFRAMETEXTURES tex_draw;   // second set the renderer draws from
} BINKTEXTURESET;

extern "C" {

struct BINKSND;
typedef S32(__stdcall *BINKSNDOPEN)(struct BINKSND *snd, U32 freq, S32 bits, S32 chans, U32 flags, HBINK bink);
typedef BINKSNDOPEN(__stdcall *BINKSNDSYSOPEN)(UINTa param);
typedef void *(__stdcall *BINKMEMALLOC)(U32 bytes);
typedef void(__stdcall *BINKMEMFREE)(void *ptr);

HBINK __stdcall BinkOpen(const char *name, U32 flags);
void __stdcall BinkClose(HBINK bnk);
char *__stdcall BinkGetError(void);
S32 __stdcall BinkDoFrame(HBINK bnk);
void __stdcall BinkNextFrame(HBINK bnk);
S32 __stdcall BinkWait(HBINK bnk);
S32 __stdcall BinkShouldSkip(HBINK bink);
S32 __stdcall BinkPause(HBINK bnk, S32 pause);
S32 __stdcall BinkControlBackgroundIO(HBINK bink, U32 control);
void __stdcall BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS *fbset);
void __stdcall BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS *fbset);
void __stdcall BinkGetRealtime(HBINK bink, BINKREALTIME *run, U32 frames);
void __stdcall BinkSetVolume(HBINK bnk, U32 trackid, S32 volume);
void __stdcall BinkSetSoundTrack(U32 total_tracks, U32 *tracks);
void __stdcall BinkSetIOSize(U32 iosize);
void __stdcall BinkSetMemory(BINKMEMALLOC a, BINKMEMFREE f);
S32 __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN open, UINTa param);
BINKSNDOPEN __stdcall BinkOpenDirectSound(UINTa param);

} // extern "C"
