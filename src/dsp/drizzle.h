/* Drizzle: shared types. One instance = one patch (plan/03 section 3). */
#ifndef DRZ_H
#define DRZ_H

#include <stdatomic.h>
#include <stdint.h>

#include "env.h"
#include "filter.h"
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
#define DRZ_VIEW (2 * DRZ_BINS)  /* a display: 128 peak levels, then 128 brightnesses */
#define DRZ_DENS_UNISON 3        /* extra detuned copies of Analog osc A (an_dens) */
#define DRZ_STEP_NEW INT64_MIN   /* voice_t.last_step: resolve against the grid at the next sub-block */

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
    float yb;           /* last osc B sample, the FM / cross-mod input */
    float wpa, wpb;     /* Wavetable phases, in samples of the 128-sample wave */
    int wa_cur, wb_cur; /* Vintage: the waves playing, changed at the end of a cycle */
    float svf1, svf2;   /* tone filter */
    uint32_t rng;       /* noise */
    float sweep_s;      /* Live: seconds since the sweep started */
    float sweep_v, lfo_v; /* the last Sweep and Source LFO values (matrix sources) */
    float lph;          /* Source LFO phase, cycles */
    float lsh0, lsh1;   /* Source LFO random values: previous and next (S&H, Smooth) */
    uint32_t lrng;
} source_t;

/* Source values derived once per block from the patch (source_prepare). */
typedef struct {
    int type, wa, wb;
    float root, sha, shb, semis_b, lvl_b, dens, ddet, fm, sync_r, noise, fold, tone;
    /* Wavetable */
    int ta, tb, wt_xmod, wt_clean;
    float wta, wtb, wt_semis_b, wt_lvl_b, wt_xamt;
    /* motion */
    float sw_timb, sw_fold, sw_pitch, sw_tone, sw_curve, sw_time;
    int sw_mode;
    int sl_wave;
    float sl_hz, sl_timb, sl_fold, sl_pitch, sl_tone;
    float m_timb, m_fold, m_tone, m_pitch; /* Live only: matrix modulation of the source (plan/04 4.3) */
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
    uint8_t bins[2][DRZ_VIEW]; /* Live and the front tape use bins[front or 0]; the render fills the other */
    int bin_next;
    float acc_e, acc_d, acc_prev; /* the tape render's running energies for the bin it is in */
    int acc_bin;
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
    env_t amp, mod;
    float vel_gain, mod_vel; /* velocity scaling of the amp level and of the Mod envelope's depths */
    float pitch, pitch_tgt;  /* the note being played, with glide (MIDI note, fractional) */
    float flav[4];           /* Flavour offsets drawn at note-on: position, pan, cents, size octaves */
    float next;              /* samples from the current sub-block start to the next onset */
    float scan_off;          /* Scan Speed travel since note-on, in buffer lengths */
    float dst[DRZ_MOD_DST_COUNT]; /* summed modulation per destination this sub-block (MD_*) */
    float src[DRZ_MOD_SRC_COUNT]; /* this voice's matrix source values (MS_*) */
    float l1ph, l1sh0, l1sh1;     /* LFO 1 phase and random values */
    float polyat, pressure, rvel, rnd;
    uint32_t polyat_age;          /* blocks since the last poly aftertouch for this voice */
    uint32_t vrng, prng;          /* grain draws and onset draws (plan/06 section 2: Random modes) */
    uint32_t rep_state;           /* vrng at the last fresh onset: repeats replay it */
    int rpt_left;
    int64_t last_step;            /* Tempo: the last grid step this voice played; DRZ_STEP_NEW after note-on */
    filt_t fl, fr;           /* per-voice filter, left and right (plan/07 section 3) */
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
    int keys_down;                 /* for the Live sweep restart and the Mod envelope's First trigger */
    uint8_t held[16];              /* keys held, oldest first (Mono and Legato) */
    int nheld;
    float last_pitch;              /* where a glide starts */
    float track_semis;             /* Live with src_track: the source's offset from its root */
    uint16_t scale_mask;           /* pitch quantise Scale: the Move set's scale (bit 0 = root) */
    int scale_root;
    int64_t clock;                 /* output samples so far (period lock's phase reference) */
    uint32_t frng;                 /* Flavour */
    /* controllers and global modulation (plan/08) */
    float wheel, breath, expr, chanat;      /* raw MIDI values */
    float s_wheel, s_breath, s_expr, s_chanat, s_mac[4], s_bend;
    float l2ph, l2sh0, l2sh1, l2_val;
    float gsrc[DRZ_MOD_SRC_COUNT];          /* global sources, and the newest voice's for global destinations */
    float gdst[DRZ_MOD_DST_COUNT];          /* modulation of the destinations that are not per voice */
    float eff[4];                           /* :effective of gr_pos, gr_size, gr_dens, flt_cut */
    src_par_t sp_live;                      /* sp with the matrix's source modulation (Live) */
    /* tempo (plan/06 section 2) */
    double beat;                            /* beat position at the start of the current sub-block */
    double int_beat;                        /* the internal clock, when the transport is stopped */
    double beat_end;                        /* where the last sub-block ended (to see the transport jump) */
    int transport;                          /* the host transport runs */
    int n_steps;                            /* Tempo grid steps in this sub-block */
    uint8_t step_off[DRZ_SUB];
    int64_t step_idx[DRZ_SUB];
    int bar_start;                          /* a bar starts in this sub-block (Random = Bar) */
    /* the Sample source (sample.c) */
    int smp_ready;                          /* smp[] holds a sample the grains may read */
    int32_t smp_frames;
    uint8_t smp_bins[DRZ_VIEW];
    int64_t step_cur;                       /* the grid step the sub-block starts in */
    float step_frac;                        /* how far into it, 0..1 */
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
    int16_t track16[DRZ_RING / DRZ_SUB]; /* Live: the source's pitch offset (cents) for each 16 samples written */
    float data[2][DRZ_RING];  /* Live ring = data[0]; Scan: front and back tapes */
    int16_t smp[DRZ_SMP_MAX]; /* M5 */
#ifdef DRZ_TEST
    int64_t onset_t[256];    /* output sample of each grain start (tests: timing) */
    int32_t onset_q[256];    /* its integer read position */
    int n_onsets;
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
/* Render n samples (n <= DRZ_SUB) at sweep phase sw (0..1, before the curve), constant over the call,
 * with the source `semis` above or below its root (src_track). Advances the Source LFO. */
void source_render(const src_par_t *p, source_t *s, float sw, float semis, float *out, int n);
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

/* mod.c (plan/08) */
float lfo_wave(int wave, float ph, float sh0, float sh1);
float lfo_hz(float knob, int sync, float bpm, float mod);
void mod_matrix(const float *pv, const float *src, float *dst);
void mod_voice(drz_inst_t *I, voice_t *v, int vi);   /* sources and destinations of a voice, per sub-block */
void mod_global(drz_inst_t *I, int n);               /* controllers, LFO 2, global destinations */
int mm_items(const drz_inst_t *I, char *buf, int cap);
uint32_t seed_hash(uint32_t a, uint32_t b);

/* drizzle.c: beats per Tempo-mode grid step (gr_div) */
float drz_step_beats(const drz_inst_t *I);

/* scale.c */
void scale_update(drz_inst_t *I);           /* read the Move set's root and scale (when Scale is in use) */
float scale_quantise(const drz_inst_t *I, float note);

/* drizzle.c */
DRZ_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
drz_inst_t *drz_inst(void *handle); /* for the native tests */
void voices_all_off(drz_inst_t *I, int hard);

#endif
