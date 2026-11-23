/* MDX360-Runtime audio: one XAudio2 source voice fed by a thread from a ring the emulator fills.
 * XAudio2 through its C interface (lpVtbl): no C++ object crosses between clang and Microsoft code.
 * The console's XAudio2 takes big-endian PCM, which is native int16 here. Method of
 * SoR2-X360 x360_snd.c + sdl_x360_core.c (console-tested); the rate control is its main.c's.
 * Strike Trilogy X360: copied from MDX360-Runtime; added mdx_audio_feed_ratio (the chip's own rate
 * goes to XAudio2, whose resampler follows SetFrequencyRatio instead of a resampler of ours). */
#include <xtl.h>
#include <xaudio2.h>
#include <string.h>

#include "mdx_x360.h"

#define RING_FRAMES 48000                  /* one second of stereo frames */
#define BUF_FRAMES  512
#define BUFFERS     4

static IXAudio2 *g_xa;
static IXAudio2MasteringVoice *g_master;
static IXAudio2SourceVoice *g_voice;
static int16_t g_ring[RING_FRAMES * 2];
static volatile LONG g_head, g_count;      /* in frames */
static CRITICAL_SECTION g_cs;
static int16_t g_bufs[BUFFERS][BUF_FRAMES * 2];

static void pull(int16_t *out, int frames) {
    int got;
    EnterCriticalSection(&g_cs);
    got = frames < g_count ? frames : g_count;
    {
        int first = got < RING_FRAMES - g_head ? got : RING_FRAMES - g_head;
        memcpy(out, g_ring + g_head * 2, (size_t)first * 4);
        memcpy(out + first * 2, g_ring, (size_t)(got - first) * 4);
    }
    g_head = (g_head + got) % RING_FRAMES;
    g_count -= got;
    LeaveCriticalSection(&g_cs);
    if (got < frames) memset(out + got * 2, 0, (size_t)(frames - got) * 4);
}

static DWORD WINAPI stream_thread(LPVOID p) {
    int next = 0;
    (void)p;
    for (;;) {
        XAUDIO2_VOICE_STATE st;
        IXAudio2SourceVoice_GetState(g_voice, &st, 0);
        while (st.BuffersQueued < BUFFERS) {
            XAUDIO2_BUFFER b;
            pull(g_bufs[next], BUF_FRAMES);
            memset(&b, 0, sizeof b);
            b.AudioBytes = BUF_FRAMES * 4;
            b.pAudioData = (const BYTE *)g_bufs[next];
            IXAudio2SourceVoice_SubmitSourceBuffer(g_voice, &b, NULL);
            next = (next + 1) % BUFFERS;
            st.BuffersQueued++;
        }
        Sleep(2);
    }
    return 0;
}

int mdx_audio_init(int rate) {
    WAVEFORMATEX f;
    HANDLE h;
    if (g_voice) return 1;
    InitializeCriticalSection(&g_cs);
    if (XAudio2Create(&g_xa, 0, XboxThread5) < 0 || !g_xa) { mdx_log("[audio] XAudio2Create failed"); return 0; }
    if (IXAudio2_CreateMasteringVoice(g_xa, &g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL) < 0) {
        mdx_log("[audio] mastering voice failed");
        return 0;
    }
    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = (DWORD)rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = f.nSamplesPerSec * 4;
    if (IXAudio2_CreateSourceVoice(g_xa, &g_voice, &f, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) < 0) {
        mdx_log("[audio] source voice failed");
        g_voice = NULL;
        return 0;
    }
    h = CreateThread(NULL, 64 * 1024, stream_thread, NULL, CREATE_SUSPENDED, NULL);
    if (h) { XSetThreadProcessor(h, 3); ResumeThread(h); CloseHandle(h); }   /* XAudio2 on thread 5; feeder on core 1 (Strike: render on 4, sound on 2) */
    IXAudio2SourceVoice_Start(g_voice, 0, XAUDIO2_COMMIT_NOW);
    mdx_log_int("[audio] XAudio2 stereo, rate", rate);
    return 1;
}

void mdx_audio_queue(const int16_t *frames, int count) {
    int tail, first;
    if (!g_voice || count <= 0) return;
    EnterCriticalSection(&g_cs);
    if (count > RING_FRAMES - g_count) count = RING_FRAMES - g_count;      /* full: the rest is dropped */
    tail = (g_head + g_count) % RING_FRAMES;
    first = count < RING_FRAMES - tail ? count : RING_FRAMES - tail;
    memcpy(g_ring + tail * 2, frames, (size_t)first * 4);
    memcpy(g_ring, frames + first * 2, (size_t)(count - first) * 4);
    g_count += count;
    LeaveCriticalSection(&g_cs);
}

int mdx_audio_queued(void) { return g_voice ? (int)g_count : 0; }

double mdx_audio_feed_frame(const int16_t *frames, int count, int rate) {
    static double fill = -1;
    static int16_t silence[2 * 4096];
    int queued = mdx_audio_queued(), frame_n = rate / 60;
    double q, target, err;
    if (queued < frame_n / 2) {                 /* ran low: a cushion of silence first */
        int pad = rate / 20 - queued;
        if (pad > 4096) pad = 4096;
        mdx_audio_queue(silence, pad);
    }
    if (mdx_audio_queued() < rate * 15 / 100) mdx_audio_queue(frames, count);   /* far behind: drop */
    q = mdx_audio_queued();
    target = rate / 20.0;
    fill = fill < 0 ? q : fill + (q - fill) * 0.05;
    err = (fill - target) / target;
    if (err > 1) err = 1;
    if (err < -1) err = -1;
    return 1.0 - 0.005 * err;
}

/* Strike Trilogy X360: queue one emulated frame at the chip rate and steer XAudio2's playback ratio
 * so the ring holds a 50 ms cushion (at most +-0.5 %, the SoR2-X360 bound). Returns the ratio. */
float mdx_audio_feed_ratio(const int16_t *frames, int count, int rate) {
    static double fill = -1;
    static int16_t silence[2 * 4096];
    static float last = 1.0f;
    int queued = mdx_audio_queued(), frame_n = rate / 60;
    double q, target, err;
    float ratio;
    if (!g_voice) return 1.0f;
    if (queued < frame_n / 2) {                 /* ran low: a cushion of silence first */
        int pad = rate / 20 - queued;
        if (pad > 4096) pad = 4096;
        if (pad > 0) mdx_audio_queue(silence, pad);
    }
    if (mdx_audio_queued() < rate * 15 / 100) mdx_audio_queue(frames, count);   /* far behind: drop */
    q = mdx_audio_queued();
    target = rate / 20.0;
    fill = fill < 0 ? q : fill + (q - fill) * 0.05;
    err = (fill - target) / target;
    if (err > 1) err = 1;
    if (err < -1) err = -1;
    ratio = (float)(1.0 + 0.005 * err);         /* fuller than the target: play a little faster */
    if (ratio != last) { IXAudio2SourceVoice_SetFrequencyRatio(g_voice, ratio, XAUDIO2_COMMIT_NOW); last = ratio; }
    return ratio;
}
