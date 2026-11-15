/* Drizzle: shared types. One instance = one patch (plan/03 section 3). */
#ifndef DRZ_H
#define DRZ_H

#include <stdatomic.h>
#include <stdint.h>

#include "env.h"
#include "fx.h"
#include "params_gen.h"
#include "plugin_api.h"

#define DRZ_VOICES 8
#define DRZ_GRAINS 128      /* shared pool (plan/06 section 1) */
#define DRZ_VOICE_GRAINS 64 /* per-voice cap */
#define DRZ_SUB 16          /* control sub-block */
#define DRZ_MAX_FRAMES 256
#ifdef DRZ_WASM
#define DRZ_MAX_INSTANCES 1 /* the browser build plays one patch */
#else
#define DRZ_MAX_INSTANCES 4
#endif
#define DRZ_RING (8 * 44100)     /* synth buffer: physical length, samples (plan/05 section 1) */
#define DRZ_SMP_MAX (30 * 44100) /* sample storage, M5 (allocated now so the .bss is measured as it will be) */
#define DRZ_XFADE 256            /* freeze seam crossfade (plan/05 section 3) */
#define DRZ_KILL_FADE 64         /* grain fade on steal, mode change, all-sound-off */
#define DRZ_TAPE_CHUNK 1024      /* Scan tape samples rendered per block: 8x realtime (plan/05 section 4) */
#define DRZ_TAPE_DEBOUNCE 2646   /* 60 ms */
#define DRZ_BINS 128             /* display bins (plan/05 section 5) */
#define DRZ_DENS_UNISON 3        /* extra detuned copies of Analog osc A (an_dens) */

#ifdef _WIN32
#define DRZ_EXPORT __declspec(dllexport)
#else
#define DRZ_EXPORT __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------ source (plan/04) */

/* One oscillator unit: master phase, and the slave phase read when sync is on. */
typedef struct {
    float tm, ts, pend; /* pend: polyBLEP correction owed to the next sample by a sync reset */
} unit_t;

typedef struct {
    unit_t a[1 + DRZ_DENS_UNISON], b;
    float yb;           /* last osc B sample, the FM input */
    float svf1, svf2;   /* tone filter */
    uint32_t rng;       /* noise */
    float sweep_s;      /* Live: seconds since the sweep started */
} source_t;

/* Source values derived once per block from the patch (source_prepare). */
typedef struct {
    int type, wa, wb;
    float root, sha, shb, semis_b, lvl_b, dens, ddet, fm, sync_r, noise, fold, tone;
    float sw_timb, sw_fold, sw_pitch, sw_tone, sw_curve, sw_time;
    int sw_mode;
} src_par_t;

/* ------------------------------------------------------------------ buffer (plan/05) */

typedef struct {
    int mode;        /* BUF_MODE_LIVE / BUF_MODE_SCAN, as last applied */
    /* Live ring (data[0]); absolute sample counts are int64, slot = abs % DRZ_RING */
    int64_t w;       /* samples written so far (the head) */
    int32_t valid;   /* samples of valid audio behind the head */
    int frozen, wait_full;
    int32_t ls, lf;  /* frozen loop: physical start and length */
    int32_t xf_k;    /* seam crossfade progress, DRZ_XFADE = done */
    /* Scan tape: front buffer read by new grains, the other one rendered */
    int front;           /* index into data[] of the tape new grains read; -1 = none yet */
    int32_t tape_len[2];
    int rendering;       /* a render into the back buffer is under way */
    int32_t render_k, render_len;
    int32_t debounce;    /* samples until a requested render starts; 0 = none pending */
    int tape_renders;    /* completed renders (tests) */
    /* display */
    uint8_t bins[2][DRZ_BINS]; /* Live and the front tape use bins[front or 0]; the render fills the other */
    int bin_next;
} buffer_t;

/* ------------------------------------------------------------------ grains (plan/06) */

typedef struct {
    int64_t pos;    /* read position, 32.32 fixed point, logical within [0, lf) */
    int64_t dpos;   /* step per output sample, 32.32; negative for reverse grains */
    int32_t ls, lf; /* region read: Live (0, DRZ_RING); frozen loop; tape (0, tape length) */
    int32_t len, age, delay;
    float wpos, winc;
    float gl, gr;
    int16_t fade;   /* samples left of a kill fade, 0 = none */
    uint8_t voice, live, buf, win, tape; /* tape: reads a Scan tape, which must never wrap */
#ifdef DRZ_TEST
    int64_t abs;    /* Live grains: absolute position of the integer read point (guard check) */
#endif
} grain_t;

/* ------------------------------------------------------------------ voices (plan/07) */

typedef struct {
    uint8_t active, gate, note, vel;
    uint32_t age;
    env_t amp;
    float vel_gain;
    float next;     /* samples from the current sub-block start to the next onset */
    float scan_off; /* Scan Speed travel since note-on, in buffer lengths */
    int n_grains;
} voice_t;

typedef struct drz_inst {
    _Atomic int in_use;
    int slot;
    float v[P_COUNT];
    int pdirty;
    char name[24];
    int preset_idx, browse_cat, browse_last;

    src_par_t sp;
    source_t src_live, src_tape;
    buffer_t buf;
    voice_t voice[DRZ_VOICES];
    grain_t grain[DRZ_GRAINS];
    uint8_t free_list[DRZ_GRAINS]; /* stack of free grain ids */
    uint8_t live[DRZ_GRAINS];      /* ids of playing grains, compact */
    int n_free, n_live;
    uint32_t rng, age_ctr;
    int cc69, sustain_down;
    uint8_t sustained[DRZ_VOICES];
    float bend;                    /* -1..1 */
    int keys_down;                 /* for the Live sweep restart */
    float vol_cur;
    float bpm;

    /* counters for tests and the benchmark */
    uint32_t grains_skipped, nan_resets, guard_violations;
    float cpu_us;

    float busl[DRZ_MAX_FRAMES], busr[DRZ_MAX_FRAMES];
    float scratch[DRZ_TAPE_CHUNK];

    /* Everything from here on is not cleared by create_instance: fx_init clears the effect arena
     * incrementally, and `valid` / `tape_len` say what of the buffers may be read. */
    fx_t fx;
    float data[2][DRZ_RING];  /* Live ring = data[0]; Scan: front and back tapes */
    int16_t smp[DRZ_SMP_MAX]; /* M5 */
#ifdef DRZ_TEST
    uint32_t tag[DRZ_RING];   /* Live: absolute index + 1 of the sample in each slot of data[0] */
#endif
} drz_inst_t;

/* params.c */
void params_defaults(drz_inst_t *I);
void params_set(drz_inst_t *I, const char *key, const char *val);
int params_get(drz_inst_t *I, const char *key, char *buf, int buf_len);
int params_find(const char *key);
float map_size_s(float x);    /* 5 ms .. 2 s, exponential */
float map_dens_hz(float x);   /* 0.5 .. 250 grains/s, exponential */
float map_buf_len_s(float x); /* 0.5 .. 8 s, exponential */
float map_scan(float x);      /* -4..4 knob -> speed, cubic */

/* source.c */
void source_prepare(drz_inst_t *I);
void source_reset(source_t *s, uint32_t seed);
/* Render n samples (n <= DRZ_TAPE_CHUNK) at sweep phase sw (0..1, before the curve), constant over the call. */
void source_render(const src_par_t *p, source_t *s, float sw, float *out, int n);
float sweep_shape(const src_par_t *p, float raw);

/* buffer.c */
void buffer_reset(drz_inst_t *I);
void buffer_block(drz_inst_t *I, int frames);
int32_t buffer_len_samples(const drz_inst_t *I);
void buffer_set_freeze(drz_inst_t *I, int on);
void buffer_set_mode(drz_inst_t *I, int mode);
void buffer_source_changed(drz_inst_t *I);
int32_t buffer_readable(const drz_inst_t *I); /* samples grains may read in the current mode */

/* grain.c */
void grains_reset(drz_inst_t *I);
void voice_spawn_grains(drz_inst_t *I, voice_t *v, int vi, int n, int64_t now0);
void grains_render(drz_inst_t *I, float (*vl)[DRZ_SUB], float (*vr)[DRZ_SUB], int n, int64_t now0);
void voice_kill_grains(drz_inst_t *I, int vi);
void grains_kill_all(drz_inst_t *I);
int grains_active(const drz_inst_t *I);
int grains_reading(const drz_inst_t *I, int buf); /* playing grains that read data[buf] as a tape */
void freeze_live_grains(drz_inst_t *I);
int grains_view(const drz_inst_t *I, char *buf, int cap);

/* drizzle.c */
DRZ_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
drz_inst_t *drz_inst(void *handle); /* for the native tests */
void voices_all_off(drz_inst_t *I, int hard);

#endif
