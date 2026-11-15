/* The Sample source (plan/04 section 5).
 *
 * A worker thread, one per process, modelled on Crest's peak_bank.c, does every file access: it reads
 * the WAV file, mixes it to mono, resamples it to 44.1 kHz and writes it into the instance's smp[].
 * The audio thread only copies a path and moves three counters, per instance slot:
 *
 *   req    set_param("smp_file") bumps it after writing the path.
 *   ack    the audio thread sets ack = req once no grain reads the old sample (they fade in 64
 *          samples): from then on the worker may write smp[].
 *   ready  the worker sets ready = req when smp[] holds the new sample (release order); a newer
 *          request makes it give up between chunks.
 *
 * No allocation anywhere: the worker decodes into one static staging buffer. */
#if defined(__linux__) && !defined(DRZ_WASM)
#define _GNU_SOURCE /* sched_setaffinity, CPU_SET */
#endif
#include "sample.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

#if !defined(DRZ_WASM)
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

#define STAGE_MAX (30 * 96000) /* source frames before resampling: 30 s at up to 96 kHz */
#define CHUNK 65536

/* ------------------------------------------------------------------ WAV decoding */

typedef struct {
    int fmt;      /* 1 PCM, 3 float */
    int channels;
    int bits;
    int32_t rate;
    int32_t data_off, data_len;
} wav_info_t;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Find fmt and data in the first `len` bytes of a file (`total` long). 0 on success. */
static int wav_parse(const uint8_t *d, int32_t len, int32_t total, wav_info_t *w, const char **err) {
    memset(w, 0, sizeof *w);
    if (len < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        *err = "not a WAV file";
        return -1;
    }
    int32_t p = 12;
    int have_fmt = 0;
    while (p + 8 <= len) {
        uint32_t size = rd32(d + p + 4);
        if (memcmp(d + p, "fmt ", 4) == 0 && p + 8 + 16 <= len) {
            const uint8_t *f = d + p + 8;
            w->fmt = rd16(f);
            w->channels = rd16(f + 2);
            w->rate = (int32_t)rd32(f + 4);
            w->bits = rd16(f + 14);
            if (w->fmt == 0xFFFE && size >= 40 && p + 8 + 26 <= len) w->fmt = rd16(f + 24); /* extensible: the subformat */
            have_fmt = 1;
        } else if (memcmp(d + p, "data", 4) == 0) {
            w->data_off = p + 8;
            int64_t avail = (int64_t)total - w->data_off;
            w->data_len = (int32_t)((int64_t)size < avail ? (int64_t)size : avail);
            break;
        }
        if (size > (uint32_t)0x7FFFFFF0) break;
        p += 8 + (int32_t)size + (int32_t)(size & 1);
    }
    if (!have_fmt || !w->data_off) {
        *err = "no audio data";
        return -1;
    }
    int ok = (w->fmt == 1 && (w->bits == 16 || w->bits == 24 || w->bits == 32)) || (w->fmt == 3 && w->bits == 32);
    if (!ok || w->channels < 1 || w->channels > 8 || w->rate < 4000 || w->rate > 192000) {
        *err = "unsupported format (PCM 16/24/32-bit or float, 1-8 channels)";
        return -1;
    }
    return 0;
}

/* Decode whole frames from `bytes` into mono int16; returns frames written. */
static int32_t decode_frames(const wav_info_t *w, const uint8_t *b, int32_t nbytes, int16_t *out, int32_t cap) {
    int bpf = w->bits / 8 * w->channels;
    int32_t n = nbytes / bpf;
    if (n > cap) n = cap;
    float g = 1.0f / (float)w->channels;
    for (int32_t i = 0; i < n; i++, b += bpf) {
        float acc = 0.0f;
        for (int c = 0; c < w->channels; c++) {
            const uint8_t *s = b + c * (w->bits / 8);
            float x;
            if (w->fmt == 3) {
                uint32_t u = rd32(s);
                float f;
                memcpy(&f, &u, sizeof f);
                x = isfinite(f) ? f : 0.0f;
            } else if (w->bits == 16) {
                x = (float)(int16_t)rd16(s) * (1.0f / 32768.0f);
            } else if (w->bits == 24) {
                int32_t v = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8;
                x = (float)v * (1.0f / 8388608.0f);
            } else {
                x = (float)(int32_t)rd32(s) * (1.0f / 2147483648.0f);
            }
            acc += x;
        }
        out[i] = (int16_t)lrintf(clampf(acc * g, -1.0f, 1.0f) * 32767.0f);
    }
    return n;
}

int32_t wav_decode(const uint8_t *data, int32_t len, int16_t *out, int32_t cap, int32_t *rate, const char **err) {
    wav_info_t w;
    if (wav_parse(data, len, len, &w, err) < 0) return -1;
    *rate = w.rate;
    return decode_frames(&w, data + w.data_off, w.data_len, out, cap);
}

/* ------------------------------------------------------------------ resampling, normalising */

int32_t smp_resample(const int16_t *in, int32_t n, int32_t rate, int16_t *out, int32_t cap) {
    if (rate == 44100) {
        int32_t m = n < cap ? n : cap;
        memcpy(out, in, (size_t)m * sizeof *out);
        return m;
    }
    double step = (double)rate / 44100.0;
    int64_t m64 = (int64_t)((double)n / step);
    int32_t m = m64 < cap ? (int32_t)m64 : cap;
    float fc = rate > 44100 ? 44100.0f / (float)rate : 1.0f; /* lowpass when downsampling */
    const int TAPS = 8;                                       /* each side: 16 taps */
    for (int32_t i = 0; i < m; i++) {
        double pos = (double)i * step;
        int32_t c = (int32_t)pos;
        float frac = (float)(pos - (double)c), acc = 0.0f, wsum = 0.0f;
        for (int k = -TAPS + 1; k <= TAPS; k++) {
            int32_t j = c + k;
            if (j < 0 || j >= n) continue;
            float x = ((float)k - frac) * fc;
            float sinc = fabsf(x) < 1e-6f ? 1.0f : sinf(DRZ_PI * x) / (DRZ_PI * x);
            float win = 0.5f + 0.5f * cosf(DRZ_PI * ((float)k - frac) / (float)TAPS);
            float wgt = sinc * win;
            acc += wgt * (float)in[j];
            wsum += wgt;
        }
        out[i] = (int16_t)lrintf(clampf(wsum > 1e-6f ? acc / wsum : 0.0f, -32767.0f, 32767.0f));
    }
    return m;
}

void smp_normalise(int16_t *x, int32_t n) {
    int pk = 0;
    for (int32_t i = 0; i < n; i++) {
        int a = x[i] < 0 ? -x[i] : x[i];
        if (a > pk) pk = a;
    }
    if (pk < 16) return;
    float g = 29205.0f / (float)pk; /* -1 dBFS */
    for (int32_t i = 0; i < n; i++) x[i] = (int16_t)lrintf(clampf((float)x[i] * g, -32767.0f, 32767.0f));
}

/* The display: 128 peak levels, then 128 brightnesses (plan/05 section 5); and for the web UI the
 * file's outline, the lowest and highest sample of each of 256 columns as signed bytes. */
static void make_env(const int16_t *x, int32_t n, int8_t *env) {
    for (int c = 0; c < DRZ_ENV; c++) {
        int32_t a = (int32_t)((int64_t)n * c / DRZ_ENV), e = (int32_t)((int64_t)n * (c + 1) / DRZ_ENV);
        int lo = 0, hi = 0;
        for (int32_t i = a; i < e; i++) {
            if (x[i] < lo) lo = x[i];
            if (x[i] > hi) hi = x[i];
        }
        env[2 * c] = (int8_t)(lo / 258);
        env[2 * c + 1] = (int8_t)(hi / 258);
    }
}

static void make_bins(const int16_t *x, int32_t n, uint8_t *bins, int8_t *env) {
    make_env(x, n, env);
    for (int b = 0; b < DRZ_BINS; b++) {
        int32_t a = (int32_t)((int64_t)n * b / DRZ_BINS), e = (int32_t)((int64_t)n * (b + 1) / DRZ_BINS);
        int pk = 0;
        float en = 0.0f, de = 0.0f, prev = a < n ? (float)x[a] : 0.0f;
        for (int32_t i = a; i < e; i++) {
            int v = x[i] < 0 ? -x[i] : x[i];
            if (v > pk) pk = v;
            float f = (float)x[i] * (1.0f / 32768.0f);
            en += f * f;
            de += (f - prev) * (f - prev);
            prev = f;
        }
        bins[b] = (uint8_t)(pk * 255 / 32767);
        bins[DRZ_BINS + b] = bright_byte(en, de);
    }
}

/* ------------------------------------------------------------------ slots and the worker */

typedef struct {
    _Atomic uint32_t req, ack, ready;
    char path[DRZ_SMP_PATH]; /* written by the audio thread before req moves */
    int norm;
    int32_t frames;          /* results, valid once ready == req */
    uint8_t bins[DRZ_VIEW];
    int8_t env[2 * DRZ_ENV];
    char status[64];
    int16_t *dst;
} smp_slot_t;

static smp_slot_t g_slot[DRZ_MAX_INSTANCES];

static void set_status(smp_slot_t *s, const char *a, const char *b) {
    snprintf(s->status, sizeof s->status, "%s%s", a, b ? b : "");
}

#if !defined(DRZ_WASM)
static pthread_t g_thread;
static _Atomic int g_running, g_quit;
static int16_t g_stage[STAGE_MAX];
static uint8_t g_chunk[CHUNK];

/* Load one request into its slot. Gives up (returns) when a newer request arrives. */
static void load(smp_slot_t *s, uint32_t r, const char *path, int norm) {
    s->frames = 0;
    memset(s->bins, 0, sizeof s->bins);
    memset(s->env, 0, sizeof s->env);
    if (!path[0]) {
        set_status(s, "none", NULL);
        atomic_store_explicit(&s->ready, r, memory_order_release);
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_status(s, "error:missing", NULL);
        atomic_store_explicit(&s->ready, r, memory_order_release);
        return;
    }
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    int32_t head = (int32_t)fread(g_chunk, 1, CHUNK, f);
    wav_info_t w;
    const char *err = NULL;
    if (total <= 0 || total > 0x7FFFFFFF || wav_parse(g_chunk, head, (int32_t)total, &w, &err) < 0) {
        fclose(f);
        set_status(s, "error:", err ? err : "unreadable");
        atomic_store_explicit(&s->ready, r, memory_order_release);
        return;
    }
    int bpf = w.bits / 8 * w.channels;
    int64_t cap_src = (int64_t)DRZ_SMP_MAX * w.rate / 44100 + 16;
    if (cap_src > STAGE_MAX) cap_src = STAGE_MAX;
    int32_t got = 0, left = w.data_len - w.data_len % bpf;
    int cut = (int64_t)left / bpf > cap_src;
    fseek(f, w.data_off, SEEK_SET);
    int32_t per = CHUNK - CHUNK % bpf;
    while (left > 0 && got < cap_src) {
        if (atomic_load(&s->req) != r) { /* a newer request: drop this one */
            fclose(f);
            return;
        }
        int32_t want = left < per ? left : per;
        int32_t n = (int32_t)fread(g_chunk, 1, (size_t)want, f);
        if (n <= 0) break;
        got += decode_frames(&w, g_chunk, n - n % bpf, g_stage + got, (int32_t)cap_src - got);
        left -= n;
    }
    fclose(f);
    if (atomic_load(&s->req) != r) return;
    int32_t frames = smp_resample(g_stage, got, w.rate, s->dst, DRZ_SMP_MAX);
    if (norm) smp_normalise(s->dst, frames);
    make_bins(s->dst, frames, s->bins, s->env);
    s->frames = frames;
    char t[32];
    snprintf(t, sizeof t, "%d%s", (int)frames, cut ? ":cut" : "");
    set_status(s, "ready:", t);
    atomic_store_explicit(&s->ready, r, memory_order_release);
}

static void *worker(void *arg) {
    (void)arg;
#ifdef __linux__
    cpu_set_t cs; /* off the audio core (3) */
    CPU_ZERO(&cs);
    for (int c = 0; c < 3; c++) CPU_SET(c, &cs);
    sched_setaffinity(0, sizeof cs, &cs);
#endif
    uint32_t done[DRZ_MAX_INSTANCES] = {0};
    char path[DRZ_SMP_PATH];
    while (!atomic_load(&g_quit)) {
        for (int i = 0; i < DRZ_MAX_INSTANCES; i++) {
            smp_slot_t *s = &g_slot[i];
            uint32_t r = atomic_load_explicit(&s->req, memory_order_acquire);
            if (r == done[i] || atomic_load_explicit(&s->ack, memory_order_acquire) != r || !s->dst) continue;
            memcpy(path, s->path, sizeof path);
            path[sizeof path - 1] = 0;
            int norm = s->norm;
            if (atomic_load(&s->req) != r) continue; /* changed while copying: next time round */
            load(s, r, path, norm);
            done[i] = r;
        }
        struct timespec ts = {0, 50 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void start_worker(void) {
    int expect = 0;
    if (!atomic_compare_exchange_strong(&g_running, &expect, 1)) return;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    /* Some runtimes (the sanitizer build of the tests) refuse a small stack: then take the default one. */
    if (pthread_create(&g_thread, &at, worker, NULL) != 0 && pthread_create(&g_thread, NULL, worker, NULL) != 0)
        atomic_store(&g_running, 0);
    pthread_attr_destroy(&at);
}

__attribute__((destructor)) static void stop_worker(void) {
    atomic_store(&g_quit, 1);
    if (atomic_load(&g_running)) pthread_join(g_thread, NULL);
}
#endif

/* ------------------------------------------------------------------ the audio side */

void smp_attach(drz_inst_t *I) {
    smp_slot_t *s = &g_slot[I->slot];
    s->dst = I->smp;
    s->path[0] = 0;
    set_status(s, "none", NULL);
    I->smp_ready = 0;
    I->smp_frames = 0;
    /* a slot's counters keep running across instances: a stale load of the previous one is dropped */
    uint32_t r = atomic_load(&s->req) + 1;
    atomic_store(&s->ack, r);
    atomic_store_explicit(&s->req, r, memory_order_release);
    atomic_store(&s->ready, r);
#if !defined(DRZ_WASM)
    start_worker();
#endif
}

void smp_request(drz_inst_t *I, const char *path) {
    smp_slot_t *s = &g_slot[I->slot];
    int n = (int)strlen(path);
    if (n >= DRZ_SMP_PATH) n = DRZ_SMP_PATH - 1;
    memcpy(s->path, path, (size_t)n);
    s->path[n] = 0;
    s->norm = I->v[P_SMP_NORM] >= 0.5f;
    I->smp_ready = 0; /* grains reading the old sample fade; smp_block acknowledges after them */
    for (int k = 0; k < I->n_live; k++)
        if (I->grain[I->live[k]].buf == 2) I->grain[I->live[k]].fade = DRZ_KILL_FADE;
#if defined(DRZ_WASM)
    set_status(s, path[0] ? "error:missing" : "none", NULL); /* the page hands the audio over itself */
#else
    set_status(s, path[0] ? "loading" : "none", NULL);
#endif
    atomic_fetch_add_explicit(&s->req, 1, memory_order_release);
}

void smp_block(drz_inst_t *I) {
    smp_slot_t *s = &g_slot[I->slot];
    uint32_t r = atomic_load_explicit(&s->req, memory_order_relaxed);
    if (atomic_load_explicit(&s->ack, memory_order_relaxed) != r && !grains_reading(I, 2))
        atomic_store_explicit(&s->ack, r, memory_order_release);
    if (!I->smp_ready && atomic_load_explicit(&s->ready, memory_order_acquire) == r && s->frames > 0) {
        I->smp_frames = s->frames;
        memcpy(I->smp_bins, s->bins, sizeof I->smp_bins);
        memcpy(I->smp_env, s->env, sizeof I->smp_env);
        I->smp_ready = 1;
    }
}

int smp_path(const drz_inst_t *I, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "%s", g_slot[I->slot].path);
    return n >= cap ? -1 : n;
}

int smp_status(const drz_inst_t *I, char *buf, int cap) {
    const smp_slot_t *s = &g_slot[I->slot];
    int n = snprintf(buf, (size_t)cap, "%s", atomic_load(&s->ready) == atomic_load(&s->req) || s->status[0] == 'e' ? s->status : "loading");
    return n >= cap ? -1 : n;
}

void smp_set_direct(drz_inst_t *I, const float *x, int32_t frames, const char *name) {
    smp_slot_t *s = &g_slot[I->slot];
    if (frames > DRZ_SMP_MAX) frames = DRZ_SMP_MAX;
    for (int k = 0; k < I->n_live; k++)
        if (I->grain[I->live[k]].buf == 2) I->grain[I->live[k]].fade = DRZ_KILL_FADE;
    I->smp_ready = 0;
    for (int32_t i = 0; i < frames; i++) I->smp[i] = (int16_t)lrintf(clampf(x[i], -1.0f, 1.0f) * 32767.0f);
    if (I->v[P_SMP_NORM] >= 0.5f) smp_normalise(I->smp, frames);
    make_bins(I->smp, frames, s->bins, s->env);
    snprintf(s->path, sizeof s->path, "%s", name ? name : "");
    s->frames = frames;
    char t[32];
    snprintf(t, sizeof t, "%d", (int)frames);
    set_status(s, "ready:", t);
    uint32_t r = atomic_load(&s->req) + 1;
    atomic_store(&s->req, r);
    atomic_store(&s->ack, r);
    atomic_store(&s->ready, r);
}
