/* The shared source (plan/04): the Wavetable type (Amino's 8-bit tables, Vintage or Clean, with
 * cross-mod) or the Analog type (Crest's band-limited waves, 2-point polyBLEP, with density, FM, sync
 * and noise), then fold and tone, moved by the Sweep and the Source LFO. Sample (M5) renders nothing
 * here: its buffer is the file. */
#include <math.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

/* ------------------------------------------------------------------ band-limited waves (Crest's osc.c) */

/* polyBLEP step residual; t is the phase since the edge in [0,1), dt the increment; dt = 0: none. */
static inline float blep_res(float t, float dt) {
    if (t < dt) {
        float u = 1.0f - t / dt;
        return -0.5f * u * u;
    }
    if (t > 1.0f - dt) {
        float u = 1.0f + (t - 1.0f) / dt;
        return 0.5f * u * u;
    }
    return 0.0f;
}

/* 2-point polyBLAMP residual, x in samples. */
static inline float blamp_res(float x) {
    if (x <= -1.0f || x >= 1.0f) return 0.0f;
    float u = x < 0.0f ? 1.0f + x : 1.0f - x;
    return u * u * u * (1.0f / 6.0f);
}

static inline float wave_sine(float t, float dt, float shape) {
    float s = sin_cycle(t);
    if (shape <= 0.001f) return s;
    float f0 = dt * DRZ_FS; /* self phase modulation, attenuated at high pitch */
    float k = 2.2f * shape * shape * (f0 > 4000.0f ? 4000.0f / f0 : 1.0f);
    return sin_cycle(t + k * s * (1.0f / DRZ_TAU));
}

static inline float wave_saw(float t, float dt, float shape) {
    float a = (2.0f * t - 1.0f) - 2.0f * blep_res(t, dt);
    if (shape <= 0.0005f) return a;
    float t2 = t + 0.5f * shape; /* "double saw": shape 1 sounds an octave up */
    if (t2 >= 1.0f) t2 -= 1.0f;
    float b = (2.0f * t2 - 1.0f) - 2.0f * blep_res(t2, dt);
    return 0.5f * (a + b);
}

static inline float wave_pulse(float t, float dt, float shape) {
    float pw = 0.5f - 0.47f * shape; /* 50 % -> 3 % */
    float J = 1.0f / (1.0f - pw);
    float naive = t < pw ? 1.0f : -pw * J;
    float tp = t - pw;
    if (tp < 0.0f) tp += 1.0f;
    return naive + J * blep_res(t, dt) - J * blep_res(tp, dt);
}

static inline float wave_tri(float t, float dt, float shape) {
    float w = clampf(0.5f - 0.49f * shape, 2.0f * dt, 1.0f - 2.0f * dt);
    float y = t < w ? -1.0f + 2.0f * t / w : 1.0f - 2.0f * (t - w) / (1.0f - w);
    if (dt > 0.0f) {
        float D = 2.0f / (w * (1.0f - w)) * dt;
        float x0 = 0.0f;
        int c0 = 1;
        if (t < dt) x0 = t / dt;
        else if (t > 1.0f - dt) x0 = (t - 1.0f) / dt;
        else c0 = 0;
        if (c0) y += D * blamp_res(x0);
        float d = t - w;
        if (d > -dt && d < dt) y -= D * blamp_res(d / dt);
    }
    return y;
}

/* dt = 0 gives the naive value (the sync jump estimate). Above 0.2 fs the waves fade to a sine. */
static inline float wave_bl(int wave, float t, float dt, float shape) {
    if (wave == AN_A_WAVE_SINE) return wave_sine(t, dt, shape);
    float sm = clampf((dt - 0.2f) * 20.0f, 0.0f, 1.0f);
    if (sm >= 1.0f) return sin_cycle(t);
    float y;
    switch (wave) {
    case AN_A_WAVE_TRI: y = wave_tri(t, dt, shape); break;
    case AN_A_WAVE_SAW: y = wave_saw(t, dt, shape); break;
    default: y = wave_pulse(t, dt, shape); break;
    }
    if (sm > 0.0f) y += sm * (sin_cycle(t) - y);
    return y;
}

static inline float frac1(float x) { return x - floorf(x); }

/* One sample of a unit, then advance it. pm: phase offset (cycles) from FM. With sync (r > 1) the
 * wave is read from a slave phase at r times the frequency that restarts when the master wraps; the
 * jump at that restart gets a polyBLEP correction split over this sample and the next. */
static inline float unit_tick(unit_t *u, int wave, float shape, float dt, float r, float pm) {
    if (r <= 1.001f) {
        float y = wave_bl(wave, pm != 0.0f ? frac1(u->tm + pm) : u->tm, dt, shape);
        u->tm += dt;
        if (u->tm >= 1.0f) u->tm -= 1.0f;
        return y;
    }
    float dts = dt * r;
    if (dts > 0.45f) dts = 0.45f;
    float y = wave_bl(wave, pm != 0.0f ? frac1(u->ts + pm) : u->ts, dts, shape) + u->pend;
    u->pend = 0.0f;
    u->tm += dt;
    u->ts += dts;
    if (u->ts >= 1.0f) u->ts -= 1.0f;
    if (u->tm >= 1.0f) {
        u->tm -= 1.0f;
        float d = u->tm / dt; /* how far past the reset the next sample lies, 0..1 */
        float before = frac1(u->ts - d * dts), after = d * dts;
        float J = wave_bl(wave, after, 0.0f, shape) - wave_bl(wave, before, 0.0f, shape);
        u->ts = after;
        y += J * 0.5f * d * d;                      /* the sample before the reset */
        u->pend = -J * 0.5f * (1.0f - d) * (1.0f - d); /* the sample after it */
    }
    return y;
}

/* ------------------------------------------------------------------ source */

void source_reset(source_t *s, uint32_t seed) {
    memset(s, 0, sizeof *s);
    static const float SPREAD[1 + DRZ_DENS_UNISON] = {0.0f, 0.13f, 0.57f, 0.81f}; /* density copies' start phases */
    for (int k = 0; k <= DRZ_DENS_UNISON; k++) s->a[k].tm = s->a[k].ts = SPREAD[k];
    s->rng = 0x2545F491u ^ (seed * 0x9E3779B1u);
    if (!s->rng) s->rng = 1;
    s->lrng = s->rng ^ 0x68E31DA4u;
    if (!s->lrng) s->lrng = 1;
    s->lsh0 = rng_bi(&s->lrng);
    s->lsh1 = rng_bi(&s->lrng);
}

void source_prepare(drz_inst_t *I) {
    const float *v = I->v;
    src_par_t *p = &I->sp;
    p->type = (int)v[P_SRC_TYPE];
    p->root = v[P_SRC_ROOT];
    p->wa = (int)v[P_AN_A_WAVE];
    p->wb = (int)v[P_AN_B_WAVE];
    p->sha = v[P_AN_A_SHAPE];
    p->shb = v[P_AN_B_SHAPE];
    p->semis_b = v[P_AN_B_SEMI] + v[P_AN_B_FINE] * 0.01f;
    p->lvl_b = v[P_AN_B_LVL];
    p->dens = v[P_AN_DENS];
    p->ddet = v[P_AN_DDET];
    p->fm = 1.5f * sqrf(v[P_AN_FM]); /* phase-modulation index in cycles (Crest's FM law) */
    p->sync_r = v[P_AN_SYNC] > 0.001f ? powf(8.0f, v[P_AN_SYNC]) : 1.0f;
    p->noise = v[P_AN_NOISE];
    p->fold = v[P_SRC_FOLD];
    p->tone = v[P_SRC_TONE];
    p->sw_timb = v[P_SW_TIMB];
    p->sw_fold = v[P_SW_FOLD];
    p->sw_pitch = v[P_SW_PITCH];
    p->sw_tone = v[P_SW_TONE];
    p->sw_curve = v[P_SW_CURVE];
    p->sw_mode = (int)v[P_SW_MODE];
    p->sw_time = 0.1f * powf(600.0f, v[P_SW_TIME]);
    p->ta = clampi((int)v[P_WT_A_TABLE], 0, DRZ_WT_TABLES - 1);
    p->tb = clampi((int)v[P_WT_B_TABLE], 0, DRZ_WT_TABLES - 1);
    p->wta = v[P_WT_A_WAVE];
    p->wtb = v[P_WT_B_WAVE];
    p->wt_semis_b = v[P_WT_B_SEMI] + v[P_WT_B_FINE] * 0.01f;
    p->wt_lvl_b = v[P_WT_B_LVL];
    p->wt_xmod = (int)v[P_WT_XMOD];
    p->wt_xamt = v[P_WT_XAMT];
    p->wt_clean = (int)v[P_WT_Q] == WT_Q_CLEAN;
    /* Source LFO: free rate 0.01..40 Hz, or a tempo division (beats per cycle) */
    static const float SL_BEATS[] = {0.0f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
    int sync = clampi((int)v[P_SL_SYNC], 0, 11);
    p->sl_hz = sync ? I->bpm / (60.0f * SL_BEATS[sync]) : 0.01f * powf(4000.0f, v[P_SL_RATE]);
    p->sl_wave = (int)v[P_SL_WAVE];
    p->sl_timb = v[P_SL_TIMB];
    p->sl_fold = v[P_SL_FOLD];
    p->sl_pitch = v[P_SL_PITCH];
    p->sl_tone = v[P_SL_TONE];
}

/* The Source LFO's value now (-1..1), then advance it by n samples. */
static float slfo_tick(const src_par_t *p, source_t *s, int n) {
    float t = s->lph, y;
    switch (p->sl_wave) {
    case SL_WAVE_TRI: y = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; break;
    case SL_WAVE_SAW: y = 2.0f * t - 1.0f; break;
    case SL_WAVE_SQUARE: y = t < 0.5f ? 1.0f : -1.0f; break;
    case SL_WAVE_S_H: y = s->lsh1; break;
    case SL_WAVE_SMOOTH: y = s->lsh0 + (s->lsh1 - s->lsh0) * (0.5f - 0.5f * sin_cycle(t * 0.5f + 0.25f)); break;
    default: y = sin_cycle(t); break;
    }
    s->lph += p->sl_hz * (float)n / DRZ_FS;
    if (s->lph >= 1.0f) {
        s->lph -= floorf(s->lph);
        s->lsh0 = s->lsh1;
        s->lsh1 = rng_bi(&s->lrng);
    }
    return y;
}

/* ------------------------------------------------------------------ Wavetable (plan/04 section 2) */

/* Vintage: 4x oversampled, no interpolation; the wave changes at the end of a cycle (Amino's stepping).
 * Clean: linear interpolation, and a fractional wave position crossfades the two nearest waves. */
static inline float wt_osc(const int8_t (*tab)[128], float *ph, int *cur, float wave, float dt, int clean, float pm) {
    float y;
    if (clean) {
        int w0 = (int)wave;
        float wf = wave - (float)w0;
        int w1 = w0 < 63 ? w0 + 1 : 63;
        float q = *ph + pm * 128.0f;
        q -= 128.0f * floorf(q * (1.0f / 128.0f));
        int j = (int)q;
        if (j > 127) j = 127;
        float fr = q - (float)j;
        int j1 = (j + 1) & 127;
        float a = (float)tab[w0][j] + ((float)tab[w0][j1] - (float)tab[w0][j]) * fr;
        float b = (float)tab[w1][j] + ((float)tab[w1][j1] - (float)tab[w1][j]) * fr;
        y = a + (b - a) * wf;
        *ph += dt;
        if (*ph >= 128.0f) *ph -= 128.0f;
    } else {
        float acc = 0.0f, d4 = 0.25f * dt;
        for (int k = 0; k < 4; k++) {
            float q = *ph + pm * 128.0f;
            q -= 128.0f * floorf(q * (1.0f / 128.0f));
            int j = (int)q;
            acc += (float)tab[*cur][j > 127 ? 127 : j];
            *ph += d4;
            if (*ph >= 128.0f) {
                *ph -= 128.0f;
                *cur = clampi((int)(wave + 0.5f), 0, 63);
            }
        }
        y = 0.25f * acc;
    }
    return y * (1.0f / 128.0f);
}

static void render_wavetable(const src_par_t *p, source_t *s, float timb, float pitch, float *out, int n) {
    float wa = clampf(p->wta + 63.0f * timb, 0.0f, 63.0f), wb = clampf(p->wtb + 63.0f * timb, 0.0f, 63.0f);
    float dta = clampf(semis_to_hz(pitch) * 128.0f / DRZ_FS, 0.0f, 60.0f);
    float dtb = clampf(semis_to_hz(pitch + p->wt_semis_b) * 128.0f / DRZ_FS, 0.0f, 60.0f);
    const int8_t (*ta)[128] = DRZ_WAVETABLES[p->ta], (*tb)[128] = DRZ_WAVETABLES[p->tb];
    int xm = p->wt_xmod;
    float xa = p->wt_xamt;
    int use_b = p->wt_lvl_b > 0.0005f || (xm != WT_XMOD_OFF && (xa > 0.0005f || xm == WT_XMOD_SYNC));
    float bmix = 1.0f / (1.0f + p->wt_lvl_b);
    for (int i = 0; i < n; i++) {
        float yb = 0.0f;
        int wrapped = 0;
        if (use_b) {
            float before = s->wpb;
            yb = wt_osc(tb, &s->wpb, &s->wb_cur, wb, dtb, p->wt_clean, 0.0f);
            wrapped = s->wpb < before;
        }
        float pm = xm == WT_XMOD_PM ? 1.5f * xa * xa * s->yb : 0.0f;
        if (xm == WT_XMOD_SYNC && wrapped) s->wpa = s->wpb * dta / (dtb > 1e-6f ? dtb : 1e-6f); /* hard sync to B */
        s->yb = yb;
        float ya = wt_osc(ta, &s->wpa, &s->wa_cur, wa, dta, p->wt_clean, pm);
        if (xm == WT_XMOD_RING) ya += (ya * yb - ya) * xa;
        out[i] = (ya + p->wt_lvl_b * yb) * bmix;
    }
}

/* Sweep curve: c > 0 bends towards exponential (slow start), c < 0 towards logarithmic. */
float sweep_shape(const src_par_t *p, float raw) {
    float x = clampf(raw, 0.0f, 1.0f), c = p->sw_curve;
    if (c > 0.001f) return powf(x, 1.0f + 3.0f * c);
    if (c < -0.001f) return 1.0f - powf(1.0f - x, 1.0f - 3.0f * c);
    return x;
}

/* Sine wavefolder (Amino's Fold): drive 1..8, faded in over the first tenth so 0 is a clean bypass. */
static inline float fold(float x, float amt) {
    float y = sin_cycle(0.25f * x * (1.0f + 7.0f * amt));
    return amt < 0.1f ? x + (y - x) * amt * 10.0f : y;
}

static void render_analog(const src_par_t *p, source_t *s, float sha, float shb, float pitch, float *out, int n) {
    float dta = clampf(semis_to_hz(pitch) / DRZ_FS, 0.0f, 0.45f);
    float dtb = clampf(semis_to_hz(pitch + p->semis_b) / DRZ_FS, 0.0f, 0.45f);
    int ncopy = p->dens > 0.001f ? DRZ_DENS_UNISON : 0;
    static const float DET[DRZ_DENS_UNISON] = {-1.0f, 1.0f, 0.5f};
    float dtc[DRZ_DENS_UNISON];
    for (int k = 0; k < ncopy; k++) dtc[k] = clampf(dta * exp2f(DET[k] * p->ddet * 35.0f / 1200.0f), 0.0f, 0.45f);
    float gcopy = p->dens, norm = 1.0f / sqrtf(1.0f + (float)ncopy * p->dens * p->dens);
    int use_b = p->lvl_b > 0.0005f || p->fm > 0.0005f;
    float bmix = 1.0f / (1.0f + p->lvl_b);
    for (int i = 0; i < n; i++) {
        float yb = 0.0f;
        if (use_b) yb = unit_tick(&s->b, p->wb, shb, dtb, 1.0f, 0.0f);
        float pm = p->fm * s->yb; /* B's previous sample: no delay-free loop */
        s->yb = yb;
        float ya = unit_tick(&s->a[0], p->wa, sha, dta, p->sync_r, pm);
        for (int k = 0; k < ncopy; k++) ya += gcopy * unit_tick(&s->a[1 + k], p->wa, sha, dtc[k], p->sync_r, pm);
        float x = (ya * norm + p->lvl_b * yb) * bmix;
        if (p->noise > 0.0005f) x += p->noise * rng_bi(&s->rng);
        out[i] = x;
    }
}

void source_render(const src_par_t *p, source_t *s, float sw, float semis, float *out, int n) {
    float sv = sweep_shape(p, sw), lv = slfo_tick(p, s, n);
    float timb = p->sw_timb * sv + p->sl_timb * lv;
    float fo = clampf(p->fold + p->sw_fold * sv + p->sl_fold * lv, 0.0f, 1.0f);
    float tone = clampf(p->tone + p->sw_tone * sv + p->sl_tone * lv, 0.0f, 1.0f);
    float pitch = p->root + semis + p->sw_pitch * sv + p->sl_pitch * lv;
    if (p->type == SRC_TYPE_WAVETABLE) {
        render_wavetable(p, s, timb, pitch, out, n);
    } else if (p->type == SRC_TYPE_ANALOG) {
        render_analog(p, s, clampf(p->sha + timb, 0.0f, 1.0f), clampf(p->shb + timb, 0.0f, 1.0f), pitch, out, n);
    } else {
        memset(out, 0, (size_t)n * sizeof(float));
        return;
    }
    if (fo > 0.0005f)
        for (int i = 0; i < n; i++) out[i] = fold(out[i], fo);
    if (tone < 0.999f) { /* tone: a 12 dB state-variable low-pass (TPT), bypassed when open */
        float fc = 150.0f * powf(20000.0f / 150.0f, tone);
        float g = tanf(DRZ_PI * fminf(fc, 0.45f * DRZ_FS) / DRZ_FS);
        const float k = 1.41421356f;
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        for (int i = 0; i < n; i++) {
            float v3 = out[i] - s->svf2;
            float v1 = a1 * s->svf1 + a2 * v3;
            float v2 = s->svf2 + a2 * s->svf1 + a3 * v3;
            s->svf1 = 2.0f * v1 - s->svf1;
            s->svf2 = 2.0f * v2 - s->svf2;
            out[i] = v2;
        }
    }
}
