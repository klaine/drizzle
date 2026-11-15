/* The grain engine, M0 (plan/06): the shared pool, the Cloud scheduler, Live and frozen reads with
 * the read guard, Hermite interpolation, a Hann window, spray, spread and the overlap gain. */
#include <math.h>

#include "drizzle.h"
#include "dsp_util.h"

void grains_reset(drz_inst_t *I) {
    for (int i = 0; i < DRZ_GRAINS; i++) I->free_list[i] = (uint8_t)(DRZ_GRAINS - 1 - i);
    I->n_free = DRZ_GRAINS;
    I->n_live = 0;
    for (int v = 0; v < DRZ_VOICES; v++) I->voice[v].n_grains = 0;
}

int grains_active(const drz_inst_t *I) { return I->n_live; }

void voice_kill_grains(drz_inst_t *I, int vi) {
    for (int k = 0; k < I->n_live; k++) {
        grain_t *g = &I->grain[I->live[k]];
        if (g->voice == vi && (g->fade == 0 || g->fade > DRZ_KILL_FADE)) g->fade = DRZ_KILL_FADE;
    }
}

/* Start one grain of voice vi at absolute (time-aligned) position `now`, `off` samples into the sub-block. */
static void spawn_one(drz_inst_t *I, voice_t *v, int vi, int off, int64_t now) {
    if (I->n_free == 0 || v->n_grains >= DRZ_VOICE_GRAINS) { /* skip, never steal (plan/06 section 1) */
        I->grains_skipped++;
        return;
    }
    const buffer_t *b = &I->buf;
    float size_s = map_size_s(I->v[P_GR_SIZE]);
    float inc = v->pitch_ratio;
    float len = size_s * DRZ_FS;
    if (len < 32.0f) len = 32.0f;
    float where = I->v[P_GR_POS] + I->v[P_GR_SPRAY] * rng_tri(&I->rng);
    int32_t q;
    float frac; /* 0..1 */
    int live = !b->frozen;
#ifdef DRZ_TEST
    int64_t abs_pos = 0;
#endif
    if (live) {
        /* The read guard (plan/05 section 2), with positions aligned to the output sample: a grain
         * starting d samples behind `now` must neither overtake the newest written sample nor fall
         * behind the oldest one still in the ring while it plays. */
        int32_t blen = buffer_len_samples(I);
        float leff = (float)(blen < b->valid ? blen : b->valid);
        float up = inc > 1.0f ? inc - 1.0f : 0.0f, down = inc < 1.0f ? 1.0f - inc : 0.0f;
        float lo = 4.0f + len * up, hi = leff - 136.0f - len * down;
        if (hi < lo) {
            float k = fabsf(inc - 1.0f);
            if (k < 1e-6f || leff < 400.0f) {
                I->grains_skipped++;
                return;
            }
            len = (leff - 140.0f) / k; /* a long grain pitched far off in a short buffer: shorten it */
            if (len < 64.0f) {
                I->grains_skipped++;
                return;
            }
            lo = 4.0f + len * up;
            hi = leff - 136.0f - len * down;
        }
        where -= floorf(where); /* spray past either end wraps round the ring */
        float d = clampf(4.0f + where * leff, lo, hi);
        float di = floorf(d);
        int64_t start = now - (int64_t)di;
        frac = 0.0f;
        if (d > di) { /* now - d = (start - 1) + (1 - fraction) */
            start -= 1;
            frac = 1.0f - (d - di);
        }
        q = (int32_t)(start % DRZ_RING);
#ifdef DRZ_TEST
        abs_pos = start;
#endif
    } else {
        if (b->lf < 4 * DRZ_XFADE) {
            I->grains_skipped++;
            return;
        }
        /* Frozen: Position is measured back from the frozen head; the loop wraps seamlessly. */
        float lf = (float)b->lf;
        float qp = lf - where * lf;
        qp -= lf * floorf(qp / lf);
        float qi = floorf(qp);
        q = (int32_t)qi;
        if (q >= b->lf) q -= b->lf;
        frac = qp - qi;
    }

    int id = I->free_list[--I->n_free];
    I->live[I->n_live++] = (uint8_t)id;
    grain_t *g = &I->grain[id];
    g->pos = ((int64_t)q << 32) + (int64_t)(frac * 4294967296.0f);
    g->dpos = (int64_t)(inc * 4294967296.0f);
    g->ls = live ? 0 : b->ls;
    g->lf = live ? DRZ_RING : b->lf;
    g->len = (int32_t)len;
    g->age = 0;
    g->delay = off;
    g->wpos = 0.0f;
    g->winc = 1.0f / (float)g->len;
    g->fade = 0;
    g->voice = (uint8_t)vi;
    g->live = (uint8_t)live;
#ifdef DRZ_TEST
    g->abs = abs_pos;
#endif
    /* Overlap gain (plan/03 section 5): incoherent grains add as sqrt(N). */
    float overlap = map_dens_hz(I->v[P_GR_DENS]) * size_s;
    float gain = 1.0f / sqrtf(overlap > 1.0f ? overlap : 1.0f);
    float pan = clampf(I->v[P_GR_SPREAD] * rng_bi(&I->rng), -1.0f, 1.0f);
    float th = (pan + 1.0f) * 0.125f; /* equal power: L = cos, R = sin of (pan + 1) pi / 4 */
    g->gl = gain * sin_cycle(th + 0.25f);
    g->gr = gain * sin_cycle(th);
    v->n_grains++;
}

void voice_spawn_grains(drz_inst_t *I, voice_t *v, int vi, int sb, int n, int64_t now0) {
    (void)sb;
    float interval = DRZ_FS / map_dens_hz(I->v[P_GR_DENS]);
    float j = I->v[P_GR_JIT];
    int sounding = v->gate || v->env > 1e-3f;
    while (v->next < (float)n) {
        int off = (int)v->next;
        if (off < 0) off = 0;
        if (sounding) spawn_one(I, v, vi, off, now0 + off);
        float u = 1.0f - rng_uni(&I->rng); /* (0, 1] */
        float iv = interval * ((1.0f - j) + j * -logf(u));
        v->next += iv < 1.0f ? 1.0f : iv;
    }
    v->next -= (float)n;
}

/* Hann window, 1 024 segments plus a guard point, linearly interpolated (plan/06 section 5). M1 moves
 * the six window tables into generated code (windows_gen.c). */
#define WIN_N 1024
static float g_hann[WIN_N + 2];

void grains_init_tables(void) {
    for (int i = 0; i <= WIN_N + 1; i++) {
        float x = (float)(i > WIN_N ? WIN_N : i) / (float)WIN_N;
        float s = sinf(DRZ_PI * x);
        g_hann[i] = s * s;
    }
}

static inline float win_at(float wpos) {
    float x = wpos * (float)WIN_N;
    int i = (int)x;
    if (i > WIN_N) i = WIN_N;
    float t = x - (float)i;
    return g_hann[i] + t * (g_hann[i + 1] - g_hann[i]);
}

static inline int32_t wrap(int32_t x, int32_t m) { return x < 0 ? x + m : (x >= m ? x - m : x); }

#define FRAC_SCALE 2.3283064365386963e-10f /* 2^-32 */

/* Render one grain from sample i to n (or its end). Returns 1 when the grain has finished.
 *
 * The fast path reads straight from the buffer: it is taken when every point the grain reads in this
 * sub-block lies inside its loop and inside the physical ring, so no index needs wrapping, and no
 * kill fade is running. Otherwise every point is wrapped (the slow path). */
static int render_one(drz_inst_t *I, grain_t *g, float *l, float *r, int i, int n, int64_t now0) {
    const float *d = I->data[0];
    const int32_t ls = g->ls, lf = g->lf;
    int m = n - i;
    if (m > g->len - g->age) m = g->len - g->age;
    int32_t q = (int32_t)(g->pos >> 32);
    int64_t span = (g->dpos * m) >> 32;
    int done = g->age + m >= g->len;
#ifdef DRZ_TEST_SLOW /* the tests run once more on the slow path alone */
    (void)q;
    (void)span;
#else
    if (!g->fade && q >= 1 && (int64_t)q + span + 3 < lf && (int64_t)ls + q + span + 3 < DRZ_RING) {
        const float *src = d + ls;
        int64_t p = g->pos, dp = g->dpos;
        float wp = g->wpos, wi = g->winc, gl = g->gl, gr = g->gr;
#ifdef DRZ_TEST
        int64_t abs0 = g->abs - q;
#endif
        for (int k = 0; k < m; k++) {
            int32_t ii = (int32_t)(p >> 32);
            float t = (float)(uint32_t)p * FRAC_SCALE;
            const float *s = src + ii;
#ifdef DRZ_TEST
            if (g->live) {
                int64_t a = abs0 + ii, now_t = now0 + i + k;
                if (I->tag[ls + ii - 1] != (uint32_t)a || I->tag[ls + ii + 2] != (uint32_t)(a + 3) || a + 2 > now_t)
                    I->guard_violations++;
            }
#endif
            float x = hermite4(s[-1], s[0], s[1], s[2], t) * win_at(wp);
            l[i + k] += x * gl;
            r[i + k] += x * gr;
            p += dp;
            wp += wi;
        }
        g->pos = p;
        g->wpos = wp;
        g->age += m;
#ifdef DRZ_TEST
        g->abs = abs0 + (int32_t)(p >> 32);
#endif
        return done;
    }
#endif
    for (int k = 0; k < m; k++) {
        int32_t q0 = (int32_t)(g->pos >> 32);
        float t = (float)(uint32_t)g->pos * FRAC_SCALE;
        int32_t a = wrap(q0 - 1, lf) + ls, b = q0 + ls, c = wrap(q0 + 1, lf) + ls, e = wrap(q0 + 2, lf) + ls;
        if (a >= DRZ_RING) a -= DRZ_RING;
        if (b >= DRZ_RING) b -= DRZ_RING;
        if (c >= DRZ_RING) c -= DRZ_RING;
        if (e >= DRZ_RING) e -= DRZ_RING;
#ifdef DRZ_TEST
        if (g->live) { /* every point read was written at the absolute position meant, and not after now */
            int64_t now_t = now0 + i + k;
            if (I->tag[a] != (uint32_t)g->abs || I->tag[b] != (uint32_t)(g->abs + 1) ||
                I->tag[c] != (uint32_t)(g->abs + 2) || I->tag[e] != (uint32_t)(g->abs + 3) || g->abs + 2 > now_t)
                I->guard_violations++;
        }
#endif
        float w = win_at(g->wpos);
        if (g->fade) {
            w *= (float)g->fade * (1.0f / (float)DRZ_KILL_FADE);
            if (--g->fade == 0) return 1;
        }
        float x = hermite4(d[a], d[b], d[c], d[e], t) * w;
        l[i + k] += x * g->gl;
        r[i + k] += x * g->gr;
        int64_t np = g->pos + g->dpos;
        int32_t nq = (int32_t)(np >> 32);
        if (nq >= lf) np -= (int64_t)lf << 32;
#ifdef DRZ_TEST
        g->abs += nq - q0;
#endif
        g->pos = np;
        g->wpos += g->winc;
        g->age++;
    }
    return done;
}

void grains_render(drz_inst_t *I, float (*vl)[DRZ_SUB], float (*vr)[DRZ_SUB], int n, int64_t now0) {
    (void)now0;
    for (int k = 0; k < I->n_live;) {
        int id = I->live[k];
        grain_t *g = &I->grain[id];
        int i = g->delay;
        g->delay = 0;
        if (render_one(I, g, vl[g->voice], vr[g->voice], i, n, now0)) {
            I->voice[g->voice].n_grains--;
            I->free_list[I->n_free++] = (uint8_t)id;
            I->live[k] = I->live[--I->n_live];
        } else {
            k++;
        }
    }
}
