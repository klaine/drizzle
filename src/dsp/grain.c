/* The grain engine (plan/06): the shared pool, the Cloud scheduler, Live / frozen / tape reads with
 * the read guard, 4-point Hermite interpolation in 32.32 fixed point, six windows, pitch and pitch
 * spray, reverse grains, spread, level spray and the overlap gain. Period lock (M2) and Tempo mode
 * (M3) are still to come. */
#include <math.h>
#include <stdio.h>

#include "drizzle.h"
#include "dsp_util.h"

#define FRAC_SCALE 2.3283064365386963e-10f /* 2^-32 */
#define TWO32 4294967296.0f

void grains_reset(drz_inst_t *I) {
    for (int i = 0; i < DRZ_GRAINS; i++) I->free_list[i] = (uint8_t)(DRZ_GRAINS - 1 - i);
    I->n_free = DRZ_GRAINS;
    I->n_live = 0;
    for (int v = 0; v < DRZ_VOICES; v++) I->voice[v].n_grains = 0;
}

int grains_active(const drz_inst_t *I) { return I->n_live; }

int grains_reading(const drz_inst_t *I, int buf) {
    int n = 0;
    for (int k = 0; k < I->n_live; k++) n += I->grain[I->live[k]].buf == buf;
    return n;
}

static void kill(grain_t *g) {
    if (g->fade == 0 || g->fade > DRZ_KILL_FADE) g->fade = DRZ_KILL_FADE;
}

void voice_kill_grains(drz_inst_t *I, int vi) {
    for (int k = 0; k < I->n_live; k++) {
        grain_t *g = &I->grain[I->live[k]];
        if (g->voice == vi) kill(g);
    }
}

void grains_kill_all(drz_inst_t *I) {
    for (int k = 0; k < I->n_live; k++) kill(&I->grain[I->live[k]]);
}

/* Freezing turns the Live grains into loop grains, so one that was reading towards the head runs on
 * into the start of the loop (which the seam crossfade continues) instead of into stale data. */
void freeze_live_grains(drz_inst_t *I) {
    const buffer_t *b = &I->buf;
    for (int k = 0; k < I->n_live; k++) {
        grain_t *g = &I->grain[I->live[k]];
        if (!g->live) continue;
        int32_t q = (int32_t)(g->pos >> 32) - b->ls;
        if (q < 0) q += DRZ_RING;
        int64_t fr = g->pos & 0xFFFFFFFF;
        g->live = 0;
        g->ls = b->ls;
        g->lf = b->lf;
        if (q < b->lf) {
            g->pos = ((int64_t)q << 32) | fr;
        } else { /* older than the loop: fade it out where it is */
            g->pos = fr;
            kill(g);
        }
    }
}

static inline float reflect01(float x) {
    float t = x - 2.0f * floorf(x * 0.5f);
    return t <= 1.0f ? t : 2.0f - t;
}

/* Quantise a pitch-spray offset (semitones) by gr_pq (plan/06 section 4). */
static float quantise(int mode, float o) {
    switch (mode) {
    case GR_PQ_OCT: return 12.0f * roundf(o / 12.0f);
    case GR_PQ_FIFTH: {
        float oct = 12.0f * floorf(o / 12.0f), r = o - oct; /* r in [0, 12): nearest of 0, 7, 12 */
        return oct + (r < 3.5f ? 0.0f : (r < 9.5f ? 7.0f : 12.0f));
    }
    case GR_PQ_SCALE: return roundf(o); /* chromatic until the Move set's scale is read (M2) */
    default: return o;
    }
}

/* Start one grain of voice vi `off` samples into the sub-block; `now` is that sample's absolute
 * (time-aligned) position in the Live ring. Every grain parameter is latched here. */
static void spawn_one(drz_inst_t *I, voice_t *v, int vi, int off, int64_t now) {
    if (I->n_free == 0 || v->n_grains >= DRZ_VOICE_GRAINS) { /* skip, never steal (plan/06 section 1) */
        I->grains_skipped++;
        return;
    }
    const float *pv = I->v;
    const buffer_t *b = &I->buf;
    float size_s = map_size_s(pv[P_GR_SIZE]);
    float len = size_s * DRZ_FS;
    if (len < 32.0f) len = 32.0f;
    float semis = (float)v->note + 12.0f * pv[P_V_OCT] + I->bend * pv[P_V_BEND] - pv[P_SRC_ROOT] + pv[P_GR_PITCH] +
                  pv[P_GR_FINE] * 0.01f;
    if (pv[P_GR_PSPR] > 0.0f) semis += quantise((int)pv[P_GR_PQ], pv[P_GR_PSPR] * rng_bi(&I->rng));
    float s = clampf(exp2f(semis * (1.0f / 12.0f)), 1.0f / 64.0f, 16.0f); /* |read step| */
    int rev = pv[P_GR_REV] > 0.0f && rng_uni(&I->rng) < pv[P_GR_REV];
    float where = pv[P_GR_POS] + pv[P_GR_SPRAY] * rng_tri(&I->rng);
    int32_t q, ls = 0, lf = DRZ_RING;
    float frac = 0.0f;
    int live = 0, bufi = 0;
#ifdef DRZ_TEST
    int64_t abs_pos = 0;
#endif
    if (b->mode == BUF_MODE_SCAN) {
        /* A tape: Position runs along the sweep; Scan and Spray reflect off the ends. */
        if (b->front < 0) {
            I->grains_skipped++;
            return;
        }
        bufi = b->front;
        lf = b->tape_len[bufi];
        float span = len * s;
        if (span > (float)lf - 16.0f) {
            len = ((float)lf - 16.0f) / s;
            if (len < 64.0f) {
                I->grains_skipped++;
                return;
            }
            span = len * s;
        }
        float x = reflect01(where + v->scan_off);
        float start = 4.0f + x * ((float)lf - span - 12.0f);
        if (rev) start += span;
        float qi = floorf(start);
        q = (int32_t)qi;
        frac = start - qi;
    } else if (!b->frozen) {
        /* The read guard (plan/05 section 2): a grain d samples behind `now` with signed step inc must
         * neither overtake the newest written sample nor fall behind the oldest one still in the ring. */
        float leff = (float)buffer_readable(I);
        float inc = rev ? -s : s;
        float up = inc > 1.0f ? inc - 1.0f : 0.0f, down = 1.0f - inc > 0.0f ? 1.0f - inc : 0.0f;
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
        float w = where - v->scan_off;
        w -= floorf(w); /* past either end wraps round the ring */
        float d = 4.0f + w * leff;
        if (rev) d -= len * s; /* the same segment, played from its end */
        d = clampf(d, lo, hi);
        float di = floorf(d);
        int64_t start = now - (int64_t)di;
        if (d > di) { /* now - d = (start - 1) + (1 - fraction) */
            start -= 1;
            frac = 1.0f - (d - di);
        }
        q = (int32_t)(start % DRZ_RING);
        live = 1;
#ifdef DRZ_TEST
        abs_pos = start;
#endif
    } else {
        /* Frozen: Position is measured back from the frozen head; the loop wraps seamlessly. */
        if (b->lf < 4 * DRZ_XFADE) {
            I->grains_skipped++;
            return;
        }
        ls = b->ls;
        lf = b->lf;
        float lff = (float)lf;
        float qp = lff * (1.0f - where + v->scan_off);
        if (rev) qp += len * s;
        qp -= lff * floorf(qp / lff);
        float qi = floorf(qp);
        q = (int32_t)qi;
        if (q >= lf) q -= lf;
        frac = qp - qi;
    }

    int id = I->free_list[--I->n_free];
    I->live[I->n_live++] = (uint8_t)id;
    grain_t *g = &I->grain[id];
    g->pos = ((int64_t)q << 32) + (int64_t)(frac * TWO32);
    g->dpos = (int64_t)(s * TWO32);
    if (rev) g->dpos = -g->dpos;
    g->ls = ls;
    g->lf = lf;
    g->len = (int32_t)len;
    g->age = 0;
    g->delay = off;
    g->wpos = 0.0f;
    g->winc = 1.0f / (float)g->len;
    g->fade = 0;
    g->voice = (uint8_t)vi;
    g->live = (uint8_t)live;
    g->buf = (uint8_t)bufi;
    g->tape = (uint8_t)(b->mode == BUF_MODE_SCAN);
    g->win = (uint8_t)clampi((int)pv[P_GR_WIN], 0, DRZ_WIN_COUNT - 1);
#ifdef DRZ_TEST
    g->abs = abs_pos;
#endif
    /* Overlap gain (plan/03 section 5): incoherent grains add as sqrt(N); the window normaliser gives
     * every window Hann's loudness. */
    float overlap = map_dens_hz(pv[P_GR_DENS]) * size_s;
    float gain = DRZ_WIN_NORM[g->win] / sqrtf(overlap > 1.0f ? overlap : 1.0f);
    if (pv[P_GR_LSPR] > 0.0f) gain *= 1.0f - pv[P_GR_LSPR] * rng_uni(&I->rng);
    float pan = clampf(pv[P_V_PAN] + pv[P_GR_SPREAD] * rng_bi(&I->rng), -1.0f, 1.0f);
    float th = (pan + 1.0f) * 0.125f; /* equal power: L = cos, R = sin of (pan + 1) pi / 4 */
    g->gl = gain * sin_cycle(th + 0.25f);
    g->gr = gain * sin_cycle(th);
    v->n_grains++;
}

void voice_spawn_grains(drz_inst_t *I, voice_t *v, int vi, int n, int64_t now0) {
    float dens = map_dens_hz(I->v[P_GR_DENS]);
    int sounding = v->gate || v->amp.y > 1e-3f;
    if (!v->gate && I->v[P_GR_RELTH] >= 0.5f) dens *= v->amp.y > 0.02f ? v->amp.y : 0.02f; /* thinning tails */
    float interval = DRZ_FS / dens;
    float j = I->v[P_GR_JIT];
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

/* ------------------------------------------------------------------ rendering */

static inline float win_at(const float *t, float wpos) {
    float x = wpos * (float)DRZ_WIN_N;
    int i = (int)x;
    if (i > DRZ_WIN_N) i = DRZ_WIN_N;
    float f = x - (float)i;
    return t[i] + f * (t[i + 1] - t[i]);
}

static inline int32_t wrap(int32_t x, int32_t m) { return x < 0 ? x + m : (x >= m ? x - m : x); }

/* Render one grain from sample i to n (or its end). Returns 1 when the grain has finished.
 *
 * The fast path reads straight from the buffer. It is taken when every point the grain reads in this
 * sub-block lies inside its region and inside the physical ring, so no index needs wrapping, and no
 * kill fade is running. Otherwise every point is wrapped (the slow path). */
static int render_one(drz_inst_t *I, grain_t *g, float *l, float *r, int i, int n, int64_t now0) {
    const float *d = I->data[g->buf];
    const float *wt = DRZ_WINDOWS[g->win];
    const int32_t ls = g->ls, lf = g->lf;
    int m = n - i;
    if (m > g->len - g->age) m = g->len - g->age;
    int done = g->age + m >= g->len;
    int32_t q = (int32_t)(g->pos >> 32);
    int64_t span = (g->dpos * m) >> 32;
    int64_t lo = span < 0 ? q + span : q, hi = span < 0 ? q : q + span;
#ifdef DRZ_TEST_SLOW /* the tests run once more on the slow path alone */
    (void)lo;
    (void)hi;
#else
    if (!g->fade && lo >= 2 && hi + 3 < lf && (int64_t)ls + hi + 3 < DRZ_RING) {
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
            float x = hermite4(s[-1], s[0], s[1], s[2], t) * win_at(wt, wp);
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
        if (g->tape && (q0 < 1 || q0 + 2 >= lf)) I->guard_violations++; /* a tape read past its ends */
#endif
        float w = win_at(wt, g->wpos);
        if (g->fade) {
            w *= (float)g->fade * (1.0f / (float)DRZ_KILL_FADE);
            if (--g->fade == 0) return 1;
        }
        float x = hermite4(d[a], d[b], d[c], d[e], t) * w;
        l[i + k] += x * g->gl;
        r[i + k] += x * g->gr;
        int64_t np = g->pos + g->dpos;
        int32_t nq = (int32_t)(np >> 32);
#ifdef DRZ_TEST
        g->abs += nq - q0;
#endif
        if (nq >= lf) np -= (int64_t)lf << 32;
        else if (nq < 0) np += (int64_t)lf << 32;
        g->pos = np;
        g->wpos += g->winc;
        g->age++;
    }
    return done;
}

void grains_render(drz_inst_t *I, float (*vl)[DRZ_SUB], float (*vr)[DRZ_SUB], int n, int64_t now0) {
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

/* grain_view (plan/11 section 4): up to 32 grains as hex "PPAA": position along the readable range and
 * the window's current level, both 00..FF. */
int grains_view(const drz_inst_t *I, char *buf, int cap) {
    static const char HEX[] = "0123456789ABCDEF";
    const buffer_t *b = &I->buf;
    int32_t len = buffer_readable(I);
    int n = 0;
    for (int k = 0; k < I->n_live && k < 32; k++) {
        if (n + 5 > cap) return -1;
        const grain_t *g = &I->grain[I->live[k]];
        int32_t q = (int32_t)(g->pos >> 32);
        float x;
        if (g->live) {
            int32_t delay = (int32_t)(b->w % DRZ_RING) - q;
            if (delay < 0) delay += DRZ_RING;
            x = len > 0 ? 1.0f - (float)delay / (float)len : 0.0f;
        } else {
            x = g->lf > 0 ? (float)q / (float)g->lf : 0.0f;
        }
        int pp = (int)(clampf(x, 0.0f, 1.0f) * 255.0f);
        int aa = (int)(clampf(win_at(DRZ_WINDOWS[g->win], g->wpos), 0.0f, 1.0f) * 255.0f);
        buf[n++] = HEX[pp >> 4];
        buf[n++] = HEX[pp & 15];
        buf[n++] = HEX[aa >> 4];
        buf[n++] = HEX[aa & 15];
    }
    if (n + 1 > cap) return -1;
    buf[n] = 0;
    return n;
}
