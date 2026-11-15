/* Modulation (plan/08): LFO 1 (per voice) and LFO 2 (global), pressure, the controllers and macros,
 * the direct depths, and the 12-slot `source x via -> destination x depth` matrix.
 *
 * Every voice sums its modulation into v->dst[MD_*] once per sub-block; the grain engine, the filter
 * and the VCA read it there. Destinations that are not per voice (the Live source, LFO 2 and Source
 * LFO rates, the effects) take the newest voice's sources and land in I->gdst. Units per destination:
 * Position, Spray, Size, Jitter, Reverse, Spread, Probability, Level Spray, Lock, Resonance, Drive,
 * Pan and the effect levels add to their 0..1 knob; Density counts 4 octaves per 1, Pitch and Pitch
 * Spray 24 semitones, Cutoff 96 semitones, Scan 4 (its knob's range), the LFO rates 4 octaves. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

#define STRIDE (P_MM02_SRC - P_MM01_SRC)
_Static_assert(STRIDE == 4 && P_MM01_VIA == P_MM01_SRC + 1 && P_MM01_DST == P_MM01_SRC + 2 &&
                   P_MM01_DEP == P_MM01_SRC + 3 && P_MM12_SRC == P_MM01_SRC + 11 * STRIDE,
               "matrix slot stride");

/* Beats per cycle of the LFO sync options (params.py LFO_SYNC). */
static const float LFO_BEATS[20] = {0.0f, 0.125f, 1.0f / 6.0f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f,
                                    1.0f, 1.5f, 4.0f / 3.0f, 2.0f, 3.0f, 4.0f, 8.0f, 12.0f, 16.0f, 32.0f, 64.0f};

uint32_t seed_hash(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x ? x : 1u;
}

/* -1..1 for LFO_WAVES: Sine, Tri, Saw Up, Saw Down, Square, S&H, Smooth. */
float lfo_wave(int wave, float ph, float sh0, float sh1) {
    switch (wave) {
    case L1_WAVE_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case L1_WAVE_SAW_UP: return 2.0f * ph - 1.0f;
    case L1_WAVE_SAW_DOWN: return 1.0f - 2.0f * ph;
    case L1_WAVE_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case L1_WAVE_S_H: return sh1;
    case L1_WAVE_SMOOTH: return sh0 + (sh1 - sh0) * (0.5f - 0.5f * sin_cycle(ph * 0.5f + 0.25f));
    default: return sin_cycle(ph);
    }
}

/* Free: 0.01..40 Hz on the knob (+ mod in knob units); synced: a division of the tempo, 4 octaves per mod. */
float lfo_hz(float knob, int sync, float bpm, float mod) {
    if (sync > 0 && sync < 20) return bpm / (60.0f * LFO_BEATS[sync]) * exp2f(4.0f * mod);
    return 0.01f * powf(4000.0f, clampf(knob + mod, 0.0f, 1.0f));
}

void mod_matrix(const float *pv, const float *src, float *dst) {
    for (int s = 0; s < DRZ_MM_SLOTS; s++) {
        int base = P_MM01_SRC + STRIDE * s;
        int a = (int)pv[base], via = (int)pv[base + 1], d = (int)pv[base + 2];
        float dep = pv[base + 3];
        if (a <= 0 || a >= DRZ_MOD_SRC_COUNT || d <= 0 || d >= DRZ_MOD_DST_COUNT || dep == 0.0f) continue;
        float x = src[a] * dep;
        if (via > 0 && via < DRZ_MOD_SRC_COUNT) x *= src[via];
        dst[d] += x;
    }
}

static void lfo_advance(float *ph, float *sh0, float *sh1, uint32_t *rng, float hz) {
    *ph += hz * DRZ_CTL_DT;
    if (*ph >= 1.0f) {
        *ph -= floorf(*ph);
        *sh0 = *sh1;
        *sh1 = rng_bi(rng);
    }
}

void mod_voice(drz_inst_t *I, voice_t *v, int vi) {
    const float *pv = I->v;
    float *s = v->src, *d = v->dst;
    memcpy(s, I->gsrc, sizeof v->src);
    /* LFO 1 */
    float l1 = lfo_wave((int)pv[P_L1_WAVE], v->l1ph, v->l1sh0, v->l1sh1);
    lfo_advance(&v->l1ph, &v->l1sh0, &v->l1sh1, &v->prng, lfo_hz(pv[P_L1_RATE], (int)pv[P_L1_SYNC], I->bpm, d[MD_LFO_1_RATE]));
    /* pressure: poly aftertouch while it arrives, else channel aftertouch */
    float tgt = v->polyat_age < 172 ? v->polyat : I->s_chanat;
    v->pressure += 0.25f * (tgt - v->pressure);
    float me = v->mod.y * v->mod_vel;
    s[MS_AMP_ENV] = v->amp.y;
    s[MS_MOD_ENV] = me;
    s[MS_LFO_1] = l1;
    s[MS_VELOCITY] = (float)v->vel * (1.0f / 127.0f);
    s[MS_REL_VELOCITY] = v->rvel;
    s[MS_KEY] = clampf((v->pitch - 60.0f) * (1.0f / 36.0f), -1.0f, 1.0f);
    s[MS_POLY_AT] = v->polyat_age < 172 ? v->polyat : 0.0f;
    s[MS_PRESSURE] = v->pressure;
    s[MS_RANDOM] = v->rnd;
    s[MS_VOICE_INDEX] = (float)vi * (1.0f / (float)(DRZ_VOICES - 1));

    memset(d, 0, sizeof v->dst);
    float l2 = I->l2_val, at = v->pressure;
    d[MD_POSITION] = pv[P_ME_POS] * me + pv[P_L1_POS] * l1 + pv[P_L2_POS] * l2 + pv[P_AT_POS] * at;
    d[MD_SIZE] = pv[P_ME_SIZE] * me + pv[P_L1_SIZE] * l1 + pv[P_AT_SIZE] * at;
    d[MD_DENSITY] = pv[P_ME_DENS] * me + pv[P_L2_DENS] * l2 + pv[P_AT_DENS] * at;
    d[MD_PITCH] = (pv[P_ME_PITCH] * me + pv[P_L1_PITCH] * l1) * (1.0f / 24.0f);
    d[MD_SPRAY] = pv[P_L2_SPRAY] * l2 + pv[P_AT_SPRAY] * at;
    d[MD_PITCH_SPRAY] = pv[P_AT_PITCH] * at;
    d[MD_PAN] = pv[P_L2_PAN] * l2;
    d[MD_CUTOFF] = pv[P_FLT_ENV] * me + pv[P_L1_CUT] * l1 * (8.0f / 96.0f) + pv[P_AT_CUT] * at * (48.0f / 96.0f);
    mod_matrix(pv, s, d);
}

/* Once per sub-block, before the voices: controllers, macros, LFO 2, and the global destinations. */
void mod_global(drz_inst_t *I, int n) {
    const float *pv = I->v;
    const float c = 1.0f - expf(-(float)n / (0.005f * DRZ_FS)); /* 5 ms smoothing */
    I->s_wheel += c * (I->wheel - I->s_wheel);
    I->s_breath += c * (I->breath - I->s_breath);
    I->s_expr += c * (I->expr - I->s_expr);
    I->s_chanat += c * (I->chanat - I->s_chanat);
    I->s_bend += c * (I->bend - I->s_bend);
    for (int m = 0; m < 4; m++) I->s_mac[m] += c * (pv[P_MAC1 + m] - I->s_mac[m]);
    /* LFO 2: global; synced to the bar while the transport runs, offset by its Phase */
    int sync = (int)pv[P_L2_SYNC];
    if (sync > 0 && I->transport) {
        double cyc = I->beat / (double)LFO_BEATS[sync] + (double)pv[P_L2_PHASE];
        float ph = (float)(cyc - floor(cyc));
        if (ph < I->l2ph) {
            I->l2sh0 = I->l2sh1;
            I->l2sh1 = rng_bi(&I->rng);
        }
        I->l2ph = ph;
    } else {
        I->l2ph += lfo_hz(pv[P_L2_RATE], sync, I->bpm, I->gdst[MD_LFO_2_RATE]) * (float)n / DRZ_FS;
        if (I->l2ph >= 1.0f) {
            I->l2ph -= floorf(I->l2ph);
            I->l2sh0 = I->l2sh1;
            I->l2sh1 = rng_bi(&I->rng);
        }
    }
    I->l2_val = lfo_wave((int)pv[P_L2_WAVE], I->l2ph, I->l2sh0, I->l2sh1);

    float *g = I->gsrc;
    memset(g, 0, sizeof I->gsrc);
    g[MS_LFO_2] = I->l2_val;
    g[MS_SOURCE_LFO] = I->src_live.lfo_v;
    g[MS_SWEEP] = I->src_live.sweep_v;
    g[MS_CHAN_AT] = I->s_chanat;
    g[MS_PRESSURE] = I->s_chanat;
    g[MS_MOD_WHEEL] = I->s_wheel;
    g[MS_BREATH] = I->s_breath;
    g[MS_EXPRESSION] = I->s_expr;
    g[MS_SUSTAIN] = I->sustain_down ? 1.0f : 0.0f;
    g[MS_BEND] = I->s_bend;
    for (int m = 0; m < 4; m++) g[MS_MACRO_1 + m] = I->s_mac[m];

    /* Global destinations use the newest voice's sources (plan/08 section 4). */
    const voice_t *last = NULL;
    for (int i = 0; i < DRZ_VOICES; i++)
        if (I->voice[i].active && (!last || I->voice[i].age > last->age)) last = &I->voice[i];
    memset(I->gdst, 0, sizeof I->gdst);
    mod_matrix(pv, last ? last->src : g, I->gdst);
    const float *gd = I->gdst;

    /* the Live source with its modulation (plan/04 section 4.3); a Scan tape never sees it */
    src_par_t *sp = &I->sp_live;
    *sp = I->sp;
    sp->m_timb = gd[MD_TIMBRE];
    sp->m_fold = gd[MD_FOLD];
    sp->m_tone = gd[MD_TONE];
    sp->m_pitch = 24.0f * gd[MD_SOURCE_PITCH];
    sp->lvl_b = clampf(sp->lvl_b + gd[MD_OSC_B_LEVEL], 0.0f, 1.0f);
    sp->wt_lvl_b = clampf(sp->wt_lvl_b + gd[MD_OSC_B_LEVEL], 0.0f, 1.0f);
    sp->wt_xamt = clampf(sp->wt_xamt + gd[MD_CROSS_MOD], 0.0f, 1.0f);
    sp->fm = 1.5f * sqrf(clampf(pv[P_AN_FM] + gd[MD_CROSS_MOD], 0.0f, 1.0f));
    sp->sl_hz *= exp2f(4.0f * gd[MD_S_LFO_RATE]);

    /* :effective (plan/08 section 6): the newest voice's modulated value */
    const float *vd = last ? last->dst : gd;
    I->eff[0] = clampf(pv[P_GR_POS] + vd[MD_POSITION], -1.0f, 2.0f);
    I->eff[1] = clampf(pv[P_GR_SIZE] + vd[MD_SIZE], 0.0f, 1.0f);
    I->eff[2] = clampf(pv[P_GR_DENS] + vd[MD_DENSITY] * 4.0f / log2f(500.0f), 0.0f, 1.0f); /* 4 octaves per unit */
    I->eff[3] = clampf(pv[P_FLT_CUT] + vd[MD_CUTOFF] * 96.0f / 115.0f, 0.0f, 1.0f);
}

/* [{"index":0,"label":"1: LFO2xWhl>Pos"},...] for the Mod Matrix list page (plan/11 section 5). */
int mm_items(const drz_inst_t *I, char *buf, int cap) {
    int n = 0;
    if (cap < 2) return -1;
    buf[n++] = '[';
    for (int m = 0; m < DRZ_MM_SLOTS; m++) {
        int base = P_MM01_SRC + STRIDE * m;
        int a = (int)I->v[base], via = (int)I->v[base + 1], d = (int)I->v[base + 2];
        char label[48], one[96];
        if (a <= 0 || d <= 0) snprintf(label, sizeof label, "%d: --", m + 1);
        else if (via > 0)
            snprintf(label, sizeof label, "%d: %sx%s>%s", m + 1, DRZ_MOD_SRC_SHORT[a], DRZ_MOD_SRC_SHORT[via], DRZ_MOD_DST_SHORT[d]);
        else snprintf(label, sizeof label, "%d: %s>%s", m + 1, DRZ_MOD_SRC_SHORT[a], DRZ_MOD_DST_SHORT[d]);
        snprintf(one, sizeof one, "%s{\"index\":%d,\"label\":\"%s\"}", m ? "," : "", m, label);
        int r = (int)strlen(one);
        if (n + r + 2 >= cap) return -1;
        memcpy(buf + n, one, (size_t)r + 1);
        n += r;
    }
    buf[n++] = ']';
    buf[n] = 0;
    return n;
}
