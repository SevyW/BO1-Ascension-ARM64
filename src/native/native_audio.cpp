// native_audio.cpp - the software mixer behind native_audio.h, on an SDL audio device.
//
// Per voice: decode (PCM16, or MS-ADPCM one block at a time), resample by linear interpolation at
// rate * pitch / 48000, an optional one-pole low-pass (occlusion), the engine's pan gains (ramped over each callback
// so that changes do not click) into a stereo float mix, clamped to 16 bits. What the XAudio2 driver also had and
// this does not yet: the reverb bus (radverb), the master EQ / limiter, the "futz" radio effect.
//
// xWMA (most of the PC game's effects: weapons, explosions, footsteps) is decoded with FFmpeg's WMA 2 decoder when
// the build has it (BO1_FFMPEG, tools/native/build-ffmpeg.sh). A buffer is decoded whole the first time the mixer
// reaches it and kept until the voice ends: a RAM sound is one buffer, a streamed one a row of windows that the
// same decoder runs through in order. Without FFmpeg such voices are not opened and stay silent.
//
// All voice state is touched under SDL's audio device lock: the callback holds it, and so does every function here.
//
// This file is not rewritten (tools/p32): its pointers are native, and it runs on SDL's audio thread.
#include "native_audio.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef BO1_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
}
#endif

namespace
{
const int kVoices = 128;
const int kOutRate = 48000;
const int kAdpcmFrames = 512;        // per block
const int kAdpcmBlockBytes = 262;    // per channel
const float kMasterGain = 0.8f;      // headroom: many voices add up

struct Buffer
{
    const uint8_t *data;
    uint32_t bytes;
    uint32_t frames;
};

struct AdpcmBlock
{
    const uint8_t *data;   // the buffer the block is from; nullptr: empty
    uint32_t index;
    int16_t samples[kAdpcmFrames * 2];
};

struct Voice
{
    bool open;
    bool started;
    bool paused;
    bool looping;
    int format;
    int channels;
    int rate;
    Buffer buffers[2];
    int head;
    int queued;
    int finished;        // buffers played to their end, not yet reported
    double position;     // in the head buffer, source frames
    double played;       // source frames since open
    float pitch;
    float gains[2][2];   // target
    float current[2][2]; // what the last callback ended with
    bool gainsValid;
    float lowpass;
    float lowpassState[2];
    AdpcmBlock blocks[2];
    int blockNext;       // which of the two decoded blocks is replaced next
};

Voice s_voices[kVoices];

#ifdef BO1_FFMPEG
// A voice's xWMA decoder and what it decoded of the voice's (at most two) buffers. Outside Voice: that is cleared
// with memset.
struct WmaState
{
    AVCodecContext *context = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    struct Decoded
    {
        const uint8_t *data = nullptr;
        std::vector<int16_t> pcm;   // interleaved
    } decoded[2];
    int next = 0;
};
WmaState *s_wma[kVoices];

void WmaFree(int voice)
{
    WmaState *w = s_wma[voice];
    if (!w)
        return;
    avcodec_free_context(&w->context);
    av_packet_free(&w->packet);
    av_frame_free(&w->frame);
    delete w;
    s_wma[voice] = nullptr;
}

bool WmaOpen(WmaState *w, int channels, int rate)
{
    av_log_set_level(AV_LOG_ERROR);   // (every start of a sound warns about timestamps it has no use for)
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_WMAV2);
    if (!codec)
        return false;
    w->context = avcodec_alloc_context3(codec);
    w->packet = av_packet_alloc();
    w->frame = av_frame_alloc();
    if (!w->context || !w->packet || !w->frame)
        return false;
    // what XAudio2 was told (iSND_CreateVoice): format tag 0x161, 6000 bytes a second per channel, these block
    // sizes, and no codec data; the codec data an xWMA file implies is flags 0, 0x1F (FFmpeg's xwma demuxer)
    av_channel_layout_default(&w->context->ch_layout, channels);
    w->context->sample_rate = rate;
    w->context->bit_rate = 48000 * channels;
    w->context->block_align = channels == 2 ? 4096 : 2230;
    w->context->extradata = (uint8_t *)av_mallocz(6 + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!w->context->extradata)
        return false;
    w->context->extradata_size = 6;
    w->context->extradata[4] = 31;
    return avcodec_open2(w->context, codec, nullptr) == 0;
}

// the decoded frames of a buffer (decoding it now if this is the first time)
const std::vector<int16_t> &WmaDecoded(Voice &v, const Buffer &b)
{
    static const std::vector<int16_t> none;
    const int voice = (int)(&v - s_voices);
    WmaState *w = s_wma[voice];
    if (!w)
    {
        w = s_wma[voice] = new WmaState;
        if (!WmaOpen(w, v.channels, v.rate))
            avcodec_free_context(&w->context);   // stays silent
    }
    for (WmaState::Decoded &d : w->decoded)
        if (d.data == b.data)
            return d.pcm;
    WmaState::Decoded &d = w->decoded[w->next];
    w->next ^= 1;
    d.data = b.data;
    d.pcm.clear();
    if (!w->context)
        return d.pcm;
    const uint32_t block = (uint32_t)w->context->block_align;
    std::vector<uint8_t> padded(block + AV_INPUT_BUFFER_PADDING_SIZE);
    d.pcm.reserve((size_t)b.frames * (size_t)v.channels + 4096);
    for (uint32_t offset = 0; offset < b.bytes; offset += block)
    {
        const uint32_t size = b.bytes - offset < block ? b.bytes - offset : block;
        memset(padded.data(), 0, padded.size());
        memcpy(padded.data(), b.data + offset, size);
        w->packet->data = padded.data();
        w->packet->size = (int)block;
        if (avcodec_send_packet(w->context, w->packet) < 0)
            continue;
        while (avcodec_receive_frame(w->context, w->frame) == 0)
        {
            const int count = w->frame->nb_samples, channels = w->frame->ch_layout.nb_channels;
            const bool planar = av_sample_fmt_is_planar((AVSampleFormat)w->frame->format) != 0;
            if (w->frame->format != AV_SAMPLE_FMT_FLTP && w->frame->format != AV_SAMPLE_FMT_FLT)
                continue;
            for (int i = 0; i < count; ++i)
                for (int c = 0; c < v.channels; ++c)
                {
                    const int from = c < channels ? c : 0;
                    const float sample = planar ? ((const float *)w->frame->extended_data[from])[i]
                                                : ((const float *)w->frame->extended_data[0])[i * channels + from];
                    const float scaled = sample * 32767.0f;
                    d.pcm.push_back((int16_t)(scaled > 32767.0f ? 32767.0f : scaled < -32768.0f ? -32768.0f : scaled));
                }
        }
    }
    return d.pcm;
}
#else
void WmaFree(int)
{
}
#endif

// how many frames a buffer has: what the engine said, or for xWMA what the decoder made of it
inline uint32_t BufferFrames(Voice &v, const Buffer &b)
{
#ifdef BO1_FFMPEG
    if (v.format == BO1_AUDIO_XWMA)
        return (uint32_t)(WmaDecoded(v, b).size() / (size_t)v.channels);
#endif
    return b.frames;
}
SDL_AudioDeviceID s_device;
bool s_tried;
float *s_mix;
int s_mixFrames;

struct Lock
{
    Lock() { if (s_device) SDL_LockAudioDevice(s_device); }
    ~Lock() { if (s_device) SDL_UnlockAudioDevice(s_device); }
};

inline int16_t ReadS16(const uint8_t *p)
{
    return (int16_t)(p[0] | (p[1] << 8));
}

// One MS-ADPCM block -> kAdpcmFrames interleaved frames. `available` bytes of it exist (a short last block: silence).
void DecodeAdpcmBlock(const uint8_t *block, uint32_t available, int channels, int16_t *out)
{
    static const int kAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
    static const int kCoef1[7] = {256, 512, 0, 192, 240, 460, 392};
    static const int kCoef2[7] = {0, -256, 0, 64, 0, -208, -232};
    const uint32_t blockBytes = (uint32_t)(kAdpcmBlockBytes * channels);
    memset(out, 0, sizeof(int16_t) * kAdpcmFrames * (size_t)channels);
    if (available < (uint32_t)(7 * channels))
        return;
    int coef1[2], coef2[2], delta[2], s1[2], s2[2];
    const uint8_t *p = block;
    for (int c = 0; c < channels; ++c)
    {
        const int predictor = *p++ % 7;
        coef1[c] = kCoef1[predictor];
        coef2[c] = kCoef2[predictor];
    }
    for (int c = 0; c < channels; ++c, p += 2)
        delta[c] = ReadS16(p);
    for (int c = 0; c < channels; ++c, p += 2)
        s1[c] = ReadS16(p);
    for (int c = 0; c < channels; ++c, p += 2)
        s2[c] = ReadS16(p);
    for (int c = 0; c < channels; ++c)
    {
        out[c] = (int16_t)s2[c];
        out[channels + c] = (int16_t)s1[c];
    }
    const uint32_t usable = available < blockBytes ? available : blockBytes;
    const uint8_t *end = block + usable;
    int frame = 2, channel = 0;
    while (p < end && frame < kAdpcmFrames)
    {
        const uint8_t byte = *p++;
        for (int half = 0; half < 2 && frame < kAdpcmFrames; ++half)
        {
            const int nibble = half == 0 ? byte >> 4 : byte & 15;
            const int value = nibble >= 8 ? nibble - 16 : nibble;
            int predicted = (s1[channel] * coef1[channel] + s2[channel] * coef2[channel]) / 256 + value * delta[channel];
            if (predicted > 32767)
                predicted = 32767;
            else if (predicted < -32768)
                predicted = -32768;
            out[frame * channels + channel] = (int16_t)predicted;
            s2[channel] = s1[channel];
            s1[channel] = predicted;
            delta[channel] = kAdapt[nibble] * delta[channel] / 256;
            if (delta[channel] < 16)
                delta[channel] = 16;
            if (++channel == channels)
            {
                channel = 0;
                ++frame;
            }
        }
    }
}

const int16_t *AdpcmFrame(Voice &v, const Buffer &b, uint32_t frame)
{
    const uint32_t index = frame / kAdpcmFrames;
    for (AdpcmBlock &cached : v.blocks)
        if (cached.data == b.data && cached.index == index)
            return &cached.samples[(frame % kAdpcmFrames) * v.channels];
    AdpcmBlock &slot = v.blocks[v.blockNext];
    v.blockNext ^= 1;
    const uint64_t offset = (uint64_t)index * kAdpcmBlockBytes * v.channels;
    slot.data = b.data;
    slot.index = index;
    DecodeAdpcmBlock(b.data + offset, offset < b.bytes ? (uint32_t)(b.bytes - offset) : 0, v.channels, slot.samples);
    return &slot.samples[(frame % kAdpcmFrames) * v.channels];
}

// one frame of a buffer as floats (silence past its data)
inline void Frame(Voice &v, const Buffer &b, uint32_t frame, float *out)
{
#ifdef BO1_FFMPEG
    if (v.format == BO1_AUDIO_XWMA)
    {
        const std::vector<int16_t> &pcm = WmaDecoded(v, b);
        const size_t at = (size_t)frame * (size_t)v.channels;
        for (int c = 0; c < v.channels; ++c)
            out[c] = at + (size_t)c < pcm.size() ? pcm[at + (size_t)c] * (1.0f / 32768.0f) : 0.0f;
        return;
    }
#endif
    if (v.format == BO1_AUDIO_MSADPCM)
    {
        const int16_t *s = AdpcmFrame(v, b, frame);
        for (int c = 0; c < v.channels; ++c)
            out[c] = s[c] * (1.0f / 32768.0f);
        return;
    }
    const uint64_t offset = (uint64_t)frame * 2 * v.channels;
    if (offset + 2 * (uint64_t)v.channels > b.bytes)
    {
        out[0] = out[1] = 0.0f;
        return;
    }
    for (int c = 0; c < v.channels; ++c)
        out[c] = ReadS16(b.data + offset + 2 * c) * (1.0f / 32768.0f);
}

void MixVoice(Voice &v, float *mix, int frames)
{
    const double step = (double)v.rate * (double)v.pitch / (double)kOutRate;
    if (!v.gainsValid)
    {
        memcpy(v.current, v.gains, sizeof(v.current));
        v.gainsValid = true;
    }
    float gain[2][2], gainStep[2][2];
    for (int c = 0; c < 2; ++c)
        for (int side = 0; side < 2; ++side)
        {
            gain[c][side] = v.current[c][side];
            gainStep[c][side] = (v.gains[c][side] - v.current[c][side]) / (float)frames;
        }
    // low-pass: blend toward a one-pole filter at roughly 1 kHz
    const float lowpass = v.lowpass < 0.0f ? 0.0f : v.lowpass > 1.0f ? 1.0f : v.lowpass;
    const float pole = 0.12f;
    for (int n = 0; n < frames; ++n)
    {
        if (!v.queued)
            break;
        const Buffer *b = &v.buffers[v.head];
        uint32_t index = (uint32_t)v.position;
        uint32_t bufferFrames = BufferFrames(v, *b);
        while (index >= bufferFrames)
        {
            // the head buffer is played
            v.position -= (double)bufferFrames;
            if (v.looping && v.queued == 1)
            {
                if (!bufferFrames)
                {
                    v.queued = 0;
                    break;
                }
            }
            else
            {
                v.head ^= 1;
                --v.queued;
                ++v.finished;
                if (!v.queued)
                {
                    v.position = 0.0;
                    break;
                }
                b = &v.buffers[v.head];
                bufferFrames = BufferFrames(v, *b);
            }
            index = (uint32_t)v.position;
        }
        if (!v.queued)
            break;
        const float fraction = (float)(v.position - (double)index);
        float a[2] = {0.0f, 0.0f}, next[2] = {0.0f, 0.0f};
        Frame(v, *b, index, a);
        if (index + 1 < bufferFrames)
            Frame(v, *b, index + 1, next);
        else if (v.looping && v.queued == 1)
            Frame(v, *b, 0, next);
        else if (v.queued == 2)
            Frame(v, v.buffers[v.head ^ 1], 0, next);
        else
            next[0] = a[0], next[1] = a[1];
        float left = 0.0f, right = 0.0f;
        for (int c = 0; c < v.channels; ++c)
        {
            float sample = a[c] + (next[c] - a[c]) * fraction;
            v.lowpassState[c] += pole * (sample - v.lowpassState[c]);
            sample += (v.lowpassState[c] - sample) * lowpass;
            left += sample * gain[c][0];
            right += sample * gain[c][1];
            gain[c][0] += gainStep[c][0];
            gain[c][1] += gainStep[c][1];
        }
        mix[2 * n] += left;
        mix[2 * n + 1] += right;
        v.position += step;
        v.played += step;
    }
    memcpy(v.current, v.gains, sizeof(v.current));
}

void Callback(void *, Uint8 *stream, int length)
{
    const int frames = length / 4;
    if (frames > s_mixFrames)
    {
        free(s_mix);
        s_mix = (float *)malloc(sizeof(float) * 2 * (size_t)frames);
        s_mixFrames = s_mix ? frames : 0;
    }
    if (!s_mix)
    {
        memset(stream, 0, (size_t)length);
        return;
    }
    memset(s_mix, 0, sizeof(float) * 2 * (size_t)frames);
    for (Voice &v : s_voices)
        if (v.open && v.started && !v.paused && v.queued && v.pitch > 0.0f)
            MixVoice(v, s_mix, frames);
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < 2 * frames; ++i)
    {
        float sample = s_mix[i] * kMasterGain;
        // soft knee above 0.8 of full scale, then the hard limit
        if (sample > 0.8f)
            sample = 0.8f + 0.2f * tanhf((sample - 0.8f) * 5.0f);
        else if (sample < -0.8f)
            sample = -0.8f + 0.2f * tanhf((sample + 0.8f) * 5.0f);
        out[i] = (int16_t)lrintf(sample * 32767.0f);
    }
}

inline Voice *Get(int voice)
{
    return voice >= 0 && voice < kVoices ? &s_voices[voice] : nullptr;
}
}

extern "C" int bo1_audio_init(void)
{
    if (s_device)
        return 1;
    if (s_tried)
        return 0;
    s_tried = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        printf("native: sound: SDL audio is not available (%s)\n", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = kOutRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;   // ~21 ms
    want.callback = Callback;
    s_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // SDL converts if the device differs
    if (!s_device)
    {
        printf("native: sound: no output device (%s)\n", SDL_GetError());
        return 0;
    }
    printf("native: sound: %s, %d Hz stereo, %d frames per callback\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
    SDL_PauseAudioDevice(s_device, 0);
    return 1;
}

extern "C" int bo1_audio_supports(int format)
{
#ifdef BO1_FFMPEG
    if (format == BO1_AUDIO_XWMA)
        return avcodec_find_decoder(AV_CODEC_ID_WMAV2) != nullptr;
#endif
    return format == BO1_AUDIO_PCM16 || format == BO1_AUDIO_MSADPCM;
}

extern "C" void bo1_audio_shutdown(void)
{
    if (s_device)
    {
        SDL_CloseAudioDevice(s_device);
        s_device = 0;
    }
    for (int voice = 0; voice < kVoices; ++voice)
        WmaFree(voice);
    memset(s_voices, 0, sizeof(s_voices));
    s_tried = false;
}

extern "C" void bo1_audio_open(int voice, int format, int channels, int rate, int looping)
{
    Voice *v = Get(voice);
    if (!v)
        return;
    Lock lock;
    WmaFree(voice);
    memset(v, 0, sizeof(*v));
    v->open = true;
    v->format = format;
    v->channels = channels < 1 ? 1 : channels > 2 ? 2 : channels;
    v->rate = rate > 0 ? rate : kOutRate;
    v->looping = looping != 0;
    v->pitch = 1.0f;
}

extern "C" int bo1_audio_queue(int voice, const void *data, unsigned bytes, unsigned frames)
{
    Voice *v = Get(voice);
    if (!v || !data)
        return 0;
    Lock lock;
    if (!v->open || v->queued >= 2)
        return 0;
    Buffer &b = v->buffers[(v->head + v->queued) & 1];
    b.data = (const uint8_t *)data;
    b.bytes = bytes;
    b.frames = frames;
    ++v->queued;
    return 1;
}

extern "C" void bo1_audio_start(int voice)
{
    Voice *v = Get(voice);
    if (!v)
        return;
    Lock lock;
    v->started = true;
}

extern "C" void bo1_audio_pause(int voice, int paused)
{
    Voice *v = Get(voice);
    if (!v)
        return;
    Lock lock;
    v->paused = paused != 0;
}

extern "C" void bo1_audio_params(int voice, float pitch, const float *gains, float lowpass)
{
    Voice *v = Get(voice);
    if (!v)
        return;
    Lock lock;
    v->pitch = pitch > 0.0f && pitch < 16.0f ? pitch : 1.0f;
    for (int c = 0; c < 2; ++c)
        for (int side = 0; side < 2; ++side)
        {
            const float g = gains[2 * c + side];
            v->gains[c][side] = g >= 0.0f && g < 8.0f ? g : 0.0f;
        }
    v->lowpass = lowpass;
}

extern "C" long long bo1_audio_progress(int voice, int *finished)
{
    Voice *v = Get(voice);
    if (finished)
        *finished = 0;
    if (!v)
        return -1;
    Lock lock;
    if (!v->open)
        return -1;
    if (finished)
    {
        *finished = v->finished;
        v->finished = 0;
    }
    return (long long)v->played;
}

extern "C" void bo1_audio_stop(int voice)
{
    Voice *v = Get(voice);
    if (!v)
        return;
    Lock lock;
    WmaFree(voice);
    memset(v, 0, sizeof(*v));
}
