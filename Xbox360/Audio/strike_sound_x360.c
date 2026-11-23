/*
 * Genesis sound hardware for the Strike Trilogy X360 port.
 *
 * Derived from Reference/StrikeHDCollection/src/hw/sound.c (MIT). The emulated side is unchanged:
 * the Z80 runs lazily on the instruction clock, the PSG and the YM2612 timers advance per output
 * sample, and every value the 68000 or the Z80 can read is computed exactly as in the reference
 * (so lockstep and save states are unaffected).
 *
 * What moved: generating the FM audio. With snd_thread_start() the game chip only takes writes
 * and runs timers (ym2612_x360.cpp); each write is logged with the index of the sample it comes
 * before, the PSG sample values are stored, and at each frame (snd_thread_flush) the batch goes to
 * a sound thread on another core, which replays the writes on the audio chip, generates the FM,
 * mixes the PSG, filters the DC and fills the output ring. Output is one frame later and is meant
 * to be sample-for-sample identical to the reference (checked on PC by Tools/pc_check).
 * Without snd_thread_start() it behaves exactly like the reference file.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef X360
#include <xtl.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "genesis.h"
#include "sound.h"
#include "ym2612_x360.h"
#include "z80.h"
#include "strike_sound_x360.h"

#define Z80_MCLK  15u                 /* Z80 runs at master / 15 */
#define PSG_MCLK  240u                /* PSG counters tick at master / 15 / 16 */
#define Z80_INT_MCLK (228u * Z80_MCLK) /* /INT is held for about one scanline */

typedef struct {
    uint16_t tone[3];
    uint8_t vol[4], noise, latch;
    int16_t cnt[4];
    uint8_t pol[4];
    uint16_t lfsr;
    uint32_t frac;                    /* master clocks not yet turned into ticks */
} psg_t;

static struct {
    z80 z;
    uint64_t z80_mclk;               /* sound hardware time, master clocks */
    uint64_t ym_next;                /* time of next YM2612 sample */
    uint64_t int_end;                /* /INT deasserts at this time */
    int busreq, reset;               /* 68000 control of the Z80 */
    uint32_t bank;                   /* Z80 $8000 window = 68000 bank << 15 */
    psg_t psg;
    int32_t dc_x[2], dc_y[2];        /* DC blocker state, 16.16 (single-thread path) */
} S;

static int32_t fm_gain = 256, psg_gain = 256;

void snd_set_levels(int fm_pct, int psg_pct)
{
    fm_gain = fm_pct * 256 / 100;
    psg_gain = psg_pct * 256 / 100;
}

/* ---------------- output ring (read by the host) ---------------- */
#define RING 16384                   /* stereo frames, ~0.3 s */
static int16_t ring[RING * 2];
static unsigned ring_w, ring_r;
static CRITICAL_SECTION ring_cs;
static int ring_cs_ready;

static void ring_init(void) { if (!ring_cs_ready) { InitializeCriticalSection(&ring_cs); ring_cs_ready = 1; } }
static void ring_lock(void) { if (ring_cs_ready) EnterCriticalSection(&ring_cs); }
static void ring_unlock(void) { if (ring_cs_ready) LeaveCriticalSection(&ring_cs); }

static void push_locked(int32_t l, int32_t r)
{
    if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
    if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
    if (ring_w - ring_r >= RING) ring_r++;           /* nobody is reading: drop oldest */
    unsigned i = ring_w++ % RING;
    ring[2 * i] = (int16_t)l;
    ring[2 * i + 1] = (int16_t)r;
}

void snd_psg_regs(uint8_t vol[4], uint16_t tone[3], int *noise)
{
    for (int i = 0; i < 4; i++) vol[i] = S.psg.vol[i];
    for (int i = 0; i < 3; i++) tone[i] = S.psg.tone[i];
    *noise = S.psg.noise;
}

int snd_available(void)
{
    ring_lock();
    int n = (int)(ring_w - ring_r);
    ring_unlock();
    return n;
}

int snd_read(int16_t *out, int max)
{
    int n = 0;
    ring_lock();
    while (n < max && ring_r != ring_w) {
        unsigned i = ring_r++ % RING;
        out[2 * n] = ring[2 * i];
        out[2 * n + 1] = ring[2 * i + 1];
        n++;
    }
    ring_unlock();
    return n;
}

/* ---------------- threaded FM: batches ---------------- */
#define BATCH_SAMPLES 4096            /* a frame is ~889 samples */
#define BATCH_LOG     16384           /* YM writes per batch (DAC streams included) */
#define BATCHES       4
#define LOG_RESET     0xFF            /* log entry: reset the audio chip */
#define LOG_RELOAD    0xFE            /* log entry: load the audio chip from reload_buf */

typedef struct { uint16_t idx; uint8_t port, v; } ymlog_t;
typedef struct {
    int n, nlog;
    int discard;                      /* hidden frames (minimap capture): mix, but drop the output */
    int32_t psg[BATCH_SAMPLES];
    ymlog_t log[BATCH_LOG];
} batch_t;

static batch_t *batches;              /* BATCHES, allocated by snd_thread_start */
static int threaded, cur;             /* cur: the batch the game is filling */
static HANDLE sem_full, sem_free, sem_idle;
static volatile LONG pending;         /* batches posted and not yet mixed */
static uint8_t reload_buf[YM_STATE_MAX];
static size_t reload_n;
static volatile LONG snd_thread_us;   /* sound thread busy time, accumulated (host reads) */
static int32_t t_dc_x[2], t_dc_y[2];  /* sound thread's DC blocker */
static int discard_now;

static void post_batch(void)
{
    if (!threaded) return;
    batch_t *b = &batches[cur];
    if (!b->n && !b->nlog) return;
    b->discard = discard_now;
    InterlockedIncrement(&pending);
    ReleaseSemaphore(sem_full, 1, NULL);
    WaitForSingleObject(sem_free, INFINITE);         /* the next free batch */
    cur = (cur + 1) % BATCHES;
    batches[cur].n = batches[cur].nlog = 0;
}

static void log_ym(uint8_t port, uint8_t v)
{
    batch_t *b = &batches[cur];
    if (b->nlog >= BATCH_LOG) { post_batch(); b = &batches[cur]; }
    b->log[b->nlog].idx = (uint16_t)b->n;
    b->log[b->nlog].port = port;
    b->log[b->nlog].v = v;
    b->nlog++;
}

static void ymw(int port, uint8_t v)
{
    ym_write(port, v);
    if (threaded) log_ym((uint8_t)(port & 3), v);
}
#define YMW(src, p, v) ymw((p), (v))

static void ym_reset_both(void)
{
    ym_reset();
    if (threaded) log_ym(LOG_RESET, 0);
}

static void apply_log(const ymlog_t *e)
{
    if (e->port == LOG_RESET) ym_audio_reset();
    else if (e->port == LOG_RELOAD) ym_audio_state_load(reload_buf, reload_n);
    else ym_audio_write(e->port, e->v);
}

static DWORD WINAPI sound_thread(LPVOID arg)
{
    static int16_t out[BATCH_SAMPLES * 2];
    int idx = 0;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    (void)arg;
    for (;;) {
        WaitForSingleObject(sem_full, INFINITE);
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        batch_t *b = &batches[idx];
        int k = 0;
        for (int i = 0; i < b->n; i++) {
            while (k < b->nlog && b->log[k].idx <= i) apply_log(&b->log[k++]);
            int32_t fm[2];
            ym_audio_sample(fm);
            int32_t ps = b->psg[i];
            int32_t in[2] = { fm[0] * 3 / 4 * fm_gain / 256 + ps * psg_gain / 256,
                              fm[1] * 3 / 4 * fm_gain / 256 + ps * psg_gain / 256 };
            for (int c = 0; c < 2; c++) {
                /* one-pole high-pass at ~5 Hz removes the DAC's DC offset (as the reference) */
                int32_t x = in[c] << 8;
                t_dc_y[c] = x - t_dc_x[c] + (int32_t)(((int64_t)t_dc_y[c] * 65225) >> 16);
                t_dc_x[c] = x;
                int32_t v = t_dc_y[c] >> 8;
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                out[2 * i + c] = (int16_t)v;
            }
        }
        while (k < b->nlog) apply_log(&b->log[k++]);   /* writes after the last sample */
        if (!b->discard) {
            ring_lock();
            for (int i = 0; i < b->n; i++) push_locked(out[2 * i], out[2 * i + 1]);
            ring_unlock();
        }
        idx = (idx + 1) % BATCHES;
        QueryPerformanceCounter(&t1);
        InterlockedExchangeAdd(&snd_thread_us, (LONG)((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart));
        ReleaseSemaphore(sem_free, 1, NULL);
        if (InterlockedDecrement(&pending) == 0) ReleaseSemaphore(sem_idle, 1, NULL);
    }
    return 0;
}

int snd_thread_start(int hw_thread)
{
    if (threaded) return 1;
    ring_init();
    batches = (batch_t *)calloc(BATCHES, sizeof *batches);
    if (!batches) return 0;
    sem_full = CreateSemaphore(NULL, 0, BATCHES, NULL);
    sem_free = CreateSemaphore(NULL, BATCHES - 1, BATCHES, NULL);   /* one batch is always the game's */
    sem_idle = CreateSemaphore(NULL, 0, 0x7FFFFFFF, NULL);
    HANDLE h = CreateThread(NULL, 64 * 1024, sound_thread, NULL, CREATE_SUSPENDED, NULL);
    if (!h) return 0;
#ifdef X360
    if (hw_thread >= 0 && hw_thread < 6) XSetThreadProcessor(h, (DWORD)hw_thread);
#else
    (void)hw_thread;
#endif
    /* The audio chip starts from the game chip as it is now (registers and timers). */
    {
        static uint8_t st[YM_STATE_MAX];
        size_t n = ym_state_save(st);
        ym_audio_reset();
        if (n) ym_audio_state_load(st, n);
    }
    memcpy(t_dc_x, S.dc_x, sizeof t_dc_x);
    memcpy(t_dc_y, S.dc_y, sizeof t_dc_y);
    cur = 0;
    batches[0].n = batches[0].nlog = 0;
    threaded = 1;
    ResumeThread(h);
    CloseHandle(h);
    return 1;
}

void snd_thread_flush(void) { post_batch(); }

void snd_thread_drain(void)
{
    if (!threaded) return;
    post_batch();
    while (pending > 0) WaitForSingleObject(sem_idle, 50);
}

uint32_t snd_thread_take_us(void) { return (uint32_t)InterlockedExchange(&snd_thread_us, 0); }
int snd_is_threaded(void) { return threaded; }

void snd_set_discard(int on)
{
    post_batch();                     /* the batch so far keeps its own flag */
    discard_now = on;
    if (!on) { ring_lock(); ring_w = ring_r = 0; ring_unlock(); }
}

/* ---------------- PSG (SN76489) ---------------- */
static int16_t psg_vol[16];

static void psg_reset(psg_t *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; i++) p->vol[i] = 15;
    p->lfsr = 0x8000;
}

static void psg_write(psg_t *p, uint8_t v)
{
    if (v & 0x80) {
        p->latch = (v >> 4) & 7;
        int ch = p->latch >> 1;
        if (p->latch & 1) p->vol[ch] = v & 15;
        else if (ch < 3) p->tone[ch] = (uint16_t)((p->tone[ch] & 0x3F0) | (v & 15));
        else { p->noise = v & 7; p->lfsr = 0x8000; }
    } else {
        int ch = p->latch >> 1;
        if (p->latch & 1) p->vol[ch] = v & 15;
        else if (ch < 3) p->tone[ch] = (uint16_t)((p->tone[ch] & 15) | ((v & 0x3F) << 4));
        else { p->noise = v & 7; p->lfsr = 0x8000; }
    }
}

int g_psg_mute = 0;           /* host: bit per PSG channel (bit 3 = noise) left out of the mix */

static int32_t psg_tick(psg_t *p)
{
    for (int ch = 0; ch < 3; ch++) {
        if (--p->cnt[ch] <= 0) {
            p->cnt[ch] = (int16_t)(p->tone[ch] ? p->tone[ch] : 1);
            p->pol[ch] ^= 1;
        }
    }
    if (--p->cnt[3] <= 0) {
        int r = p->noise & 3;
        p->cnt[3] = (int16_t)(r == 3 ? (p->tone[2] ? p->tone[2] : 1) : (0x10 << r));
        p->pol[3] ^= 1;
        if (p->pol[3]) {                              /* shift on rising edge */
            unsigned fb = (p->noise & 4) ? ((p->lfsr ^ (p->lfsr >> 3)) & 1) : (p->lfsr & 1);
            p->lfsr = (uint16_t)((p->lfsr >> 1) | (fb << 15));
        }
    }
    int32_t s = 0;
    for (int ch = 0; ch < 3; ch++) {
        if (g_psg_mute & (1 << ch)) continue;
        if (p->tone[ch] <= 1) s += psg_vol[p->vol[ch]];   /* tone 0/1: steady level (sample playback) */
        else s += p->pol[ch] ? psg_vol[p->vol[ch]] : -psg_vol[p->vol[ch]];
    }
    if (!(g_psg_mute & 8)) s += (p->lfsr & 1) ? psg_vol[p->vol[3]] : -psg_vol[p->vol[3]];
    return s;
}

/* ---------------- Z80 bus ---------------- */
static uint8_t z_read(void *ud, uint16_t a)
{
    (void)ud;
    if (a < 0x4000) return g_zram[a & 0x1FFF];
    if (a < 0x6000) return ym_status();
    if (a < 0x8000) return 0xFF;
    uint32_t ma = (S.bank << 15) | (a & 0x7FFF);
    if (ma >= 0xA00000 && ma < 0xA10000) return 0xFF;
    return (uint8_t)gen_read8(ma);
}

static void bank_write(uint8_t v)
{
    S.bank = ((S.bank >> 1) | ((uint32_t)(v & 1) << 8)) & 0x1FF;
}

static void z_write(void *ud, uint16_t a, uint8_t v)
{
    (void)ud;
    if (a < 0x4000) { g_zram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { YMW("z80", a & 3, v); return; }
    if (a < 0x6100) { bank_write(v); return; }
    if (a >= 0x7F10 && a < 0x7F18) { if (a & 1) psg_write(&S.psg, v); return; }
    if (a >= 0x8000) {
        uint32_t ma = (S.bank << 15) | (a & 0x7FFF);
        if (ma >= 0xE00000) gen_write8(ma, v);          /* 68000 RAM only */
    }
}

static uint8_t z_in(z80 *z, uint8_t port) { (void)z; (void)port; return 0xFF; }
static void z_out(z80 *z, uint8_t port, uint8_t v) { (void)z; (void)port; (void)v; }

static void z80_hook(void)
{
    S.z.read_byte = z_read;
    S.z.write_byte = z_write;
    S.z.port_in = z_in;
    S.z.port_out = z_out;
    S.z.userdata = 0;
}

static void z80_power(void)
{
    z80_init(&S.z);
    z80_hook();
}

/* ---------------- time ---------------- */
static inline uint64_t now_mclk(void)
{
    return g_frame * SND_FRAME_MCLK + (g_icount - g_frame_start) * SND_FRAME_MCLK / g_frame_len;
}

static void gen_sample(void)
{
    int32_t ps = 0, n = 0;
    if (!threaded) {                                  /* the reference path */
        int32_t fm[2];
        ym_sample(fm);
        S.psg.frac += SND_SAMPLE_MCLK;
        while (S.psg.frac >= PSG_MCLK) { ps += psg_tick(&S.psg); n++; S.psg.frac -= PSG_MCLK; }
        if (n) ps /= n;
        int32_t in[2] = { fm[0] * 3 / 4 * fm_gain / 256 + ps * psg_gain / 256,
                          fm[1] * 3 / 4 * fm_gain / 256 + ps * psg_gain / 256 };
        for (int c = 0; c < 2; c++) {
            int32_t x = in[c] << 8;
            S.dc_y[c] = x - S.dc_x[c] + (int32_t)(((int64_t)S.dc_y[c] * 65225) >> 16);
            S.dc_x[c] = x;
            in[c] = S.dc_y[c] >> 8;
        }
        ring_lock(); push_locked(in[0], in[1]); ring_unlock();
        S.ym_next += SND_SAMPLE_MCLK;
        return;
    }
    ym_tick();                                        /* timers only: the audio chip generates */
    S.psg.frac += SND_SAMPLE_MCLK;
    while (S.psg.frac >= PSG_MCLK) { ps += psg_tick(&S.psg); n++; S.psg.frac -= PSG_MCLK; }
    if (n) ps /= n;
    batch_t *b = &batches[cur];
    if (b->n >= BATCH_SAMPLES) { post_batch(); b = &batches[cur]; }
    b->psg[b->n++] = ps;
    S.ym_next += SND_SAMPLE_MCLK;
}

static int z80_running(void) { return !S.reset && !S.busreq; }

static void run_to(uint64_t target)
{
    while (S.z80_mclk < target) {
        if (z80_running()) {
            unsigned long c0 = S.z.cyc;
            z80_step(&S.z);
            S.z80_mclk += (uint64_t)(S.z.cyc - c0) * Z80_MCLK;
        } else {
            S.z80_mclk = target;
        }
        if (S.z.int_pending && S.z80_mclk >= S.int_end) S.z.int_pending = 0;
        while (S.ym_next <= S.z80_mclk) gen_sample();
    }
}

void snd_sync(void) { run_to(now_mclk()); }

void snd_vblank(void)
{
    snd_sync();
    if (z80_running()) {
        z80_gen_int(&S.z, 0xFF);
        S.int_end = S.z80_mclk + Z80_INT_MCLK;
    }
}

/* ---------------- setup ---------------- */
void snd_init(void)
{
    ring_init();
    ym_init();
    for (int i = 0; i < 15; i++) {
        double v = 2400.0;
        for (int k = 0; k < i; k++) v *= 0.794328;    /* -2 dB per step */
        psg_vol[i] = (int16_t)v;
    }
    psg_vol[15] = 0;
}

void snd_reset(void)
{
    static int inited;
    if (!inited) { snd_init(); inited = 1; }
    memset(&S, 0, sizeof S);
    z80_power();
    psg_reset(&S.psg);
    ym_reset_both();
    S.reset = 1;
    S.ym_next = SND_SAMPLE_MCLK;
    if (threaded) {
        snd_thread_drain();
        memset(t_dc_x, 0, sizeof t_dc_x);
        memset(t_dc_y, 0, sizeof t_dc_y);
    }
    ring_lock(); ring_w = ring_r = 0; ring_unlock();
}

/* ---------------- 68000 side ---------------- */
uint8_t snd_68k_read8(uint32_t a)
{
    snd_sync();
    a &= 0xFFFF;
    if (a < 0x4000) return g_zram[a & 0x1FFF];
    if (a < 0x6000) return ym_status();
    return 0xFF;
}

void snd_68k_write8(uint32_t a, uint8_t v)
{
    snd_sync();
    a &= 0xFFFF;
    if (a < 0x4000) { g_zram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { YMW("68k", a & 3, v); return; }
    if (a < 0x6100) { bank_write(v); return; }
    if (a >= 0x7F10 && a < 0x7F18 && (a & 1)) psg_write(&S.psg, v);
}

void snd_psg_write(uint8_t v)
{
    snd_sync();
    psg_write(&S.psg, v);
}

void snd_set_busreq(int on)
{
    snd_sync();
    S.busreq = on;
}

void snd_set_reset(int asserted)
{
    snd_sync();
    if (asserted && !S.reset) {
        z80_power();
        ym_reset_both();
    }
    S.reset = asserted;
}

int snd_bus_granted(void) { return S.busreq && !S.reset; }

/* ---------------- save states ---------------- */
size_t snd_state_size(void) { return sizeof S + 4 + YM_STATE_MAX; }

void snd_state_save(uint8_t *out)
{
    memcpy(out, &S, sizeof S);
    uint32_t n = (uint32_t)ym_state_save(out + sizeof S + 4);
    memcpy(out + sizeof S, &n, 4);
}

int snd_state_load(const uint8_t *in)
{
    memcpy(&S, in, sizeof S);
    z80_hook();
    uint32_t n;
    memcpy(&n, in + sizeof S, 4);
    if (threaded) {
        /* The audio chip takes the loaded registers before the next batch's first sample. The
         * game chip never runs its envelopes, so notes in flight restart; the music goes on with
         * the next key-on (only after a state load). */
        snd_thread_drain();
        if (n <= sizeof reload_buf) {
            memcpy(reload_buf, in + sizeof S + 4, n);
            reload_n = n;
            log_ym(LOG_RELOAD, 0);
        }
        memset(t_dc_x, 0, sizeof t_dc_x);
        memset(t_dc_y, 0, sizeof t_dc_y);
    }
    ring_lock(); ring_w = ring_r = 0; ring_unlock();
    return ym_state_load(in + sizeof S + 4, n);
}
