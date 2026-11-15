/* Drizzle: shared types. One instance = one patch (plan/03 section 3). */
#ifndef DRZ_H
#define DRZ_H

#include <stdatomic.h>
#include <stdint.h>

#include "params.h"
#include "plugin_api.h"

#define DRZ_VOICES 8
#define DRZ_GRAINS 128      /* shared pool (plan/06 section 1) */
#define DRZ_VOICE_GRAINS 64 /* per-voice cap */
#define DRZ_SUB 16          /* control sub-block */
#define DRZ_MAX_FRAMES 256
#define DRZ_MAX_INSTANCES 4
#define DRZ_RING (8 * 44100)    /* synth buffer: physical ring length, samples (plan/05 section 1) */
#define DRZ_SMP_MAX (30 * 44100) /* sample storage, M5 (allocated now so M0 measures the real .bss) */
#define DRZ_XFADE 256            /* freeze seam crossfade (plan/05 section 3) */
#define DRZ_KILL_FADE 64         /* grain fade on steal / all-sound-off */

#ifdef _WIN32
#define DRZ_EXPORT __declspec(dllexport)
#else
#define DRZ_EXPORT __attribute__((visibility("default")))
#endif

/* The Analog source, M0: one polyBLEP oscillator at the root (plan/04 section 3). */
typedef struct {
    float phase;
} source_t;

/* The Live ring and its freeze state (plan/05 sections 2-3). Absolute sample counts are int64: the
 * physical slot of absolute position a is a % DRZ_RING. */
typedef struct {
    int64_t w;        /* samples written so far (the head) */
    int32_t valid;    /* samples of valid audio behind the head, <= DRZ_RING */
    int frozen;       /* the head is stopped */
    int wait_full;    /* freeze once the buffer holds buf_len (after a state load) */
    int32_t ls, lf;   /* frozen loop: physical start and length */
    int32_t xf_k;     /* seam crossfade progress, DRZ_XFADE = done */
} buffer_t;

typedef struct {
    int64_t pos;    /* read position, 32.32 fixed point, logical within the grain's loop [0, lf) */
    int64_t dpos;   /* step per output sample, 32.32 */
    int32_t ls, lf;  /* the region read: Live (0, DRZ_RING), frozen (loop start, loop length) */
    int32_t len, age, delay;
    float wpos, winc;
    float gl, gr;
    int16_t fade; /* samples left of a kill fade, 0 = none */
    uint8_t voice, live;
#ifdef DRZ_TEST
    int64_t abs; /* absolute position of q, Live grains only: the guard check */
#endif
} grain_t;

typedef struct {
    uint8_t active, gate, note, vel;
    uint32_t age;
    float env;         /* amp envelope level, M0: a fixed attack / release */
    float next;        /* samples from the current sub-block start to the next onset */
    float pitch_ratio; /* read step of this note's grains */
    int n_grains;
} voice_t;

typedef struct drz_inst {
    _Atomic int in_use;
    int slot;
    float v[P_COUNT];
    int pdirty;
    char name[24];

    source_t src;
    buffer_t buf;
    voice_t voice[DRZ_VOICES];
    grain_t grain[DRZ_GRAINS];
    uint8_t free_list[DRZ_GRAINS]; /* stack of free grain ids */
    uint8_t live[DRZ_GRAINS];      /* ids of playing grains, compact */
    int n_free, n_live;
    uint32_t rng, age_ctr;
    int cc69;      /* last CC 69 state, for the rising edge */
    int sustain_down;
    uint8_t sustained[DRZ_VOICES]; /* note-offs held by the pedal */
    float vol_cur; /* master gain ramp */

    /* counters for tests and the benchmark */
    uint32_t grains_skipped, nan_resets, guard_violations;
    float cpu_us;

    float busl[DRZ_MAX_FRAMES], busr[DRZ_MAX_FRAMES];
    float scratch[DRZ_SUB]; /* one sub-block of source audio */

    /* The big arrays last: nothing clears them (plan/01 section 3). `valid` says what may be read. */
    float data[2][DRZ_RING]; /* A: Live ring; B: Scan back buffer (M1) */
    int16_t smp[DRZ_SMP_MAX];  /* M5 */
#ifdef DRZ_TEST
    uint32_t tag[DRZ_RING]; /* absolute index + 1 of the sample in each slot */
#endif
} drz_inst_t;

/* buffer.c */
void buffer_reset(drz_inst_t *I);
void buffer_write_block(drz_inst_t *I, int frames);
int32_t buffer_len_samples(const drz_inst_t *I);
void buffer_set_freeze(drz_inst_t *I, int on);

/* grain.c */
void grains_init_tables(void); /* once, from move_plugin_init_v2 */
void grains_reset(drz_inst_t *I);
void voice_spawn_grains(drz_inst_t *I, voice_t *v, int vi, int sb, int n, int64_t now0);
void grains_render(drz_inst_t *I, float (*vl)[DRZ_SUB], float (*vr)[DRZ_SUB], int n, int64_t now0);
void voice_kill_grains(drz_inst_t *I, int vi);
int grains_active(const drz_inst_t *I);

/* source.c */
void source_render(drz_inst_t *I, float *out, int n);

/* drizzle.c */
DRZ_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
drz_inst_t *drz_inst(void *handle); /* for the native tests */

#endif
