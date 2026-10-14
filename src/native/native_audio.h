// native_audio.h - the native build's sound output: a software mixer on an SDL audio device (native_audio.cpp).
//
// src/web/web_sound.cpp (the SD_* driver API) owns the engine side: which voice plays which buffer, when it starts,
// its pitch and its pan matrix. This is the part that makes it audible. A voice is a queue of up to two buffers
// (one for a RAM sound, the two stream windows for a streamed one) that the mixer decodes, resamples and mixes on
// SDL's audio thread; the engine asks how far it got (bo1_audio_progress) to release stream windows and end voices,
// exactly where the XAudio2 driver asked its source voice.
//
// The buffers are the engine's memory: a buffer must stay valid until the mixer reported it finished or the voice
// was stopped (bo1_audio_stop returns only when the mixer no longer touches the voice).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    BO1_AUDIO_PCM16 = 0,     // interleaved signed 16-bit
    BO1_AUDIO_MSADPCM = 1,   // Microsoft ADPCM, blocks of 262 bytes per channel (512 frames), the standard 7 coefficients
    BO1_AUDIO_XWMA = 2,      // xWMA: WMA version 2 packets of 2230 bytes (mono) or 4096 bytes (stereo), 48 kbit/s per
                             // channel, as XAudio2 was given them; needs FFmpeg's decoder (bo1_audio_supports)
};

// Opens the output (48 kHz stereo). 0 when there is none (no device, a dedicated server, +set bo1_nosound): the
// caller keeps its own clock then. Safe to call again.
int bo1_audio_init(void);
void bo1_audio_shutdown(void);

// Whether the mixer can decode a format (BO1_AUDIO_XWMA: only in a build with FFmpeg, cmake BO1_NATIVE_FFMPEG).
int bo1_audio_supports(int format);

// A new voice (replaces whatever the slot had): silent until buffers are queued and it is started.
void bo1_audio_open(int voice, int format, int channels, int rate, int looping);
// Appends a buffer of `frames` frames in `bytes` bytes. At most two are held; returns 0 if there was no room.
int bo1_audio_queue(int voice, const void *data, unsigned bytes, unsigned frames);
void bo1_audio_start(int voice);
void bo1_audio_pause(int voice, int paused);
// pitch: the playback rate factor. gains[2 * channel + side]: input channel -> left (0) / right (1).
// lowpass: 0 = open .. 1 = fully filtered (occlusion).
void bo1_audio_params(int voice, float pitch, const float *gains, float lowpass);
// Frames played since the voice was opened (-1: no such voice); *finished receives how many queued buffers were
// played to their end since the previous call (each is no longer referenced by the mixer).
long long bo1_audio_progress(int voice, int *finished);
void bo1_audio_stop(int voice);

#ifdef __cplusplus
}
#endif
