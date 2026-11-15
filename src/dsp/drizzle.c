/* Drizzle: Schwung sound-generator module. Plugin entry points, instance pool, voices, MIDI and the
 * render loop (plan/01 section 2, plan/03 section 2, plan/07). */
#ifdef DRZ_PROFILE
#define _POSIX_C_SOURCE 200809L /* clock_gettime for cpu_us */
#endif
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef DRZ_PROFILE
#include <time.h>
#endif

#include "drizzle.h"
#include "dsp_util.h"

#define VOICE_GAIN 0.35f /* fixed per voice, never normalised by the voice count (plan/03 section 5) */

static const host_api_v1_t *g_host;
static drz_inst_t g_pool[DRZ_MAX_INSTANCES]; /* .bss, no malloc: about 6.5 MB each (plan/03 section 3) */
static uint8_t g_ever_used[DRZ_MAX_INSTANCES];
static plugin_api_v2_t g_api;
static char g_err[64];

/* ------------------------------------------------------------------ lifecycle */

static drz_inst_t *claim(drz_inst_t *p) {
    int expect = 0;
    if (!atomic_compare_exchange_strong(&p->in_use, &expect, 1)) return NULL;
    int slot = (int)(p - g_pool);
    /* Clear everything in front of the effects and buffers (a few tens of KB). The effect arena is
     * cleared incrementally by fx_init, the buffers never: `valid` and `tape_len` say what is readable. */
    memset((char *)p + offsetof(drz_inst_t, slot), 0, offsetof(drz_inst_t, fx) - offsetof(drz_inst_t, slot));
    memset(&p->fx, 0, offsetof(fx_t, mem));
    fx_init(&p->fx, g_ever_used[slot]);
    g_ever_used[slot] = 1;
    p->slot = slot;
    p->rng = 0x9E3779B1u * (uint32_t)(slot + 1) | 1u;
    p->bpm = 120.0f;
    p->frng = 0x51ED270Bu * (uint32_t)(slot + 1) | 1u;
    p->scale_mask = 0x0FFF;
    params_defaults(p);
    source_prepare(p);
    buffer_reset(p);
    grains_reset(p);
    p->vol_cur = 2.0f * sqrf(p->v[P_PATCH_VOL]);
    return p;
}

static void *drz_create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    for (int i = 0; i < DRZ_MAX_INSTANCES; i++) {
        drz_inst_t *p = claim(&g_pool[i]);
        if (p) {
            scale_update(p); /* the first move_info read resolves the export here, not on the audio callback */
            if (json_defaults && json_defaults[0] == '{') params_set(p, "state", json_defaults);
            return p;
        }
    }
    snprintf(g_err, sizeof g_err, "Drizzle: all %d instances are in use", DRZ_MAX_INSTANCES);
    return NULL;
}

drz_inst_t *drz_inst(void *handle) {
    drz_inst_t *c = (drz_inst_t *)handle;
    if (c >= g_pool && c < g_pool + DRZ_MAX_INSTANCES && atomic_load(&c->in_use)) return c;
    return NULL;
}

static void drz_destroy_instance(void *instance) {
    drz_inst_t *I = drz_inst(instance);
    if (I) atomic_store(&I->in_use, 0);
}

static int drz_get_error(void *instance, char *buf, int buf_len) {
    const char *e = drz_inst(instance) ? "" : g_err;
    int n = (int)strlen(e);
    if (n == 0 || !buf || buf_len < n + 1) return 0;
    memcpy(buf, e, (size_t)n + 1);
    return n;
}

/* ------------------------------------------------------------------ voices (plan/07 section 1) */

static int voices_sounding(const drz_inst_t *I) {
    int n = 0;
    for (int i = 0; i < DRZ_VOICES; i++) n += I->voice[i].active;
    return n;
}

static float glide_time(const drz_inst_t *I) {
    float x = I->v[P_V_GLIDE];
    return x > 0.001f ? 0.001f * powf(2000.0f, x) : 0.0f;
}

/* Start (or restart) voice v on `note`. `legato`: a held mono voice moves to a new key, so its
 * envelopes, Scan travel and grains carry on and only the pitch moves. */
static void voice_start(drz_inst_t *I, voice_t *v, int note, int vel, int legato) {
    int vi = (int)(v - I->voice);
    float target = (float)note + 12.0f * I->v[P_V_OCT];
    int glide = glide_time(I) > 0.0f && (I->v[P_V_MODE] != V_MODE_LEGATO || legato);
    if (!v->active) {
        v->amp.y = 0.0f;
        v->amp.stage = ES_IDLE;
        v->mod.y = 0.0f;
        v->mod.stage = ES_IDLE;
        filt_reset(&v->fl);
        filt_reset(&v->fr);
    } else if (v->note != note && !legato) {
        voice_kill_grains(I, vi); /* a stolen or retaken voice's grains fade in 64 samples */
    }
    v->pitch_tgt = target;
    v->pitch = glide && I->last_pitch > 0.0f ? I->last_pitch : target;
    I->last_pitch = target;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->age = ++I->age_ctr;
    float vn = (float)vel * (1.0f / 127.0f);
    v->vel_gain = 1.0f - I->v[P_AE_VEL] * (1.0f - vn * vn);
    v->mod_vel = 1.0f - I->v[P_ME_VEL] * (1.0f - vn);
    if (legato) return;
    v->next = 0.0f; /* the first grain starts at note-on */
    v->scan_off = 0.0f;
    float f = I->v[P_V_FLAV]; /* Flavour: small per-note offsets (plan/07 section 1) */
    v->flav[0] = 0.1f * f * rng_bi(&I->frng);
    v->flav[1] = 0.5f * f * rng_bi(&I->frng);
    v->flav[2] = 15.0f * f * rng_bi(&I->frng);
    v->flav[3] = 0.5f * f * rng_bi(&I->frng);
    env_trigger(&v->amp, 0);
    if (I->v[P_ME_TRIG] != ME_TRIG_FIRST || I->keys_down <= 1 || v->mod.stage == ES_IDLE) env_trigger(&v->mod, 0);
}

static void release_voice(drz_inst_t *I, voice_t *v) {
    (void)I;
    v->gate = 0;
    env_release(&v->amp);
    env_release(&v->mod);
}

static void held_remove(drz_inst_t *I, int note) {
    int o = 0;
    for (int i = 0; i < I->nheld; i++)
        if (I->held[i] != note) I->held[o++] = I->held[i];
    I->nheld = o;
}

static void note_on(drz_inst_t *I, int note, int vel) {
    if (I->keys_down == 0) I->src_live.sweep_s = 0.0f; /* the Live sweep restarts with a new phrase */
    held_remove(I, note);
    if (I->nheld == (int)sizeof I->held) held_remove(I, I->held[0]);
    I->held[I->nheld++] = (uint8_t)note;
    I->keys_down++;
    int mode = (int)I->v[P_V_MODE];
    if (mode != V_MODE_POLY) { /* Mono / Legato: one voice, the newest key */
        voice_t *v = &I->voice[0];
        for (int i = 1; i < DRZ_VOICES; i++)
            if (I->voice[i].active) release_voice(I, &I->voice[i]);
        voice_start(I, v, note, vel, mode == V_MODE_LEGATO && v->active && v->gate);
        I->sustained[0] = 0;
        return;
    }
    int limit = clampi((int)I->v[P_V_VOICES], 1, DRZ_VOICES);
    voice_t *pick = NULL;
    for (int i = 0; i < DRZ_VOICES && !pick; i++) /* the same key again: reuse its voice */
        if (I->voice[i].active && I->voice[i].note == note) pick = &I->voice[i];
    if (!pick && voices_sounding(I) < limit)
        for (int i = 0; i < DRZ_VOICES && !pick; i++)
            if (!I->voice[i].active) pick = &I->voice[i];
    if (!pick) { /* steal: the quietest released voice, else the oldest */
        for (int i = 0; i < DRZ_VOICES; i++) {
            voice_t *v = &I->voice[i];
            if (v->active && !v->gate && (!pick || v->amp.y < pick->amp.y)) pick = v;
        }
        if (!pick)
            for (int i = 0; i < DRZ_VOICES; i++)
                if (I->voice[i].active && (!pick || I->voice[i].age < pick->age)) pick = &I->voice[i];
    }
    if (!pick) return;
    voice_start(I, pick, note, vel, 0);
    I->sustained[pick - I->voice] = 0;
}

static void note_off(drz_inst_t *I, int note) {
    held_remove(I, note);
    if (I->keys_down > 0) I->keys_down--;
    if (I->v[P_V_MODE] != V_MODE_POLY) {
        voice_t *v = &I->voice[0];
        if (!v->active || !v->gate || v->note != note) return;
        if (I->nheld > 0) { /* back to the newest key still held, legato */
            voice_start(I, v, I->held[I->nheld - 1], v->vel, 1);
            return;
        }
        if (I->sustain_down) I->sustained[0] = 1;
        else release_voice(I, v);
        return;
    }
    for (int i = 0; i < DRZ_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (v->active && v->gate && v->note == note) {
            if (I->sustain_down) I->sustained[i] = 1;
            else release_voice(I, v);
        }
    }
}

void voices_all_off(drz_inst_t *I, int hard) {
    for (int i = 0; i < DRZ_VOICES; i++) {
        voice_t *v = &I->voice[i];
        I->sustained[i] = 0;
        if (v->gate) release_voice(I, v);
        if (hard) {
            voice_kill_grains(I, i);
            v->amp.y = v->mod.y = 0.0f;
            v->amp.stage = v->mod.stage = ES_IDLE;
        }
    }
    I->keys_down = 0;
    I->nheld = 0;
}

/* Per sub-block: glide, the Mod envelope and its direct depths into dst[] (M3 adds LFOs, pressure and
 * the matrix to the same sums), and the filter's coefficients. */
static void voice_control(drz_inst_t *I, voice_t *v, const env_par_t *ap, const env_par_t *mp, float glide_c) {
    const float *pv = I->v;
    env_tick(&v->amp, ap);
    float me = env_tick(&v->mod, mp) * v->mod_vel;
    v->pitch = v->pitch_tgt + (v->pitch - v->pitch_tgt) * glide_c;
    float *d = v->dst;
    memset(d, 0, sizeof v->dst);
    d[MD_POSITION] = pv[P_ME_POS] * me;
    d[MD_SIZE] = pv[P_ME_SIZE] * me;
    d[MD_DENSITY] = pv[P_ME_DENS] * me;
    d[MD_PITCH] = pv[P_ME_PITCH] * me * (1.0f / 24.0f);
    d[MD_CUTOFF] = pv[P_FLT_ENV] * me;
}

/* The voice's filter over one sub-block of its grains (plan/07 section 3). */
static void voice_filter(drz_inst_t *I, voice_t *v, float *l, float *r, int n) {
    static const int MAP[] = {-1, FLT_LP24, FLT_LP12, FLT_BP12, FLT_HP12};
    int type = clampi((int)I->v[P_FLT_TYPE], 0, 4);
    if (type == FLT_TYPE_OFF) return;
    const float *pv = I->v;
    float note = 20.0f + 115.0f * clampf(pv[P_FLT_CUT], 0.0f, 1.0f) + 96.0f * v->dst[MD_CUTOFF] +
                 pv[P_FLT_KEY] * (v->pitch - 60.0f) + pv[P_FLT_VEL] * 48.0f * (float)v->vel * (1.0f / 127.0f);
    float hz = fminf(semis_to_hz(clampf(note, 0.0f, 140.0f)), 0.45f * DRZ_FS);
    float g = tanf(DRZ_PI * hz / DRZ_FS);
    float res = clampf(pv[P_FLT_RES] + v->dst[MD_RESONANCE], 0.0f, 1.0f);
    float drive = clampf(pv[P_FLT_DRIVE] + v->dst[MD_FILTER_DRIVE], 0.0f, 1.0f);
    float k = filt_res_to_k(res), gin = (1.0f + 0.5f * res) * (1.0f + 2.0f * drive), gout = 1.0f / (1.0f + drive);
    for (int i = 0; i < n; i++) {
        l[i] *= gin;
        r[i] *= gin;
    }
    filt_process(&v->fl, MAP[type], l, n, g, g, k, 0.3f + 2.0f * drive, &I->rng, NULL);
    filt_process(&v->fr, MAP[type], r, n, g, g, k, 0.3f + 2.0f * drive, &I->rng, NULL);
    for (int i = 0; i < n; i++) {
        l[i] *= gout;
        r[i] *= gout;
    }
}

/* ------------------------------------------------------------------ MIDI (plan/01 section 6) */

static void drz_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    drz_inst_t *I = drz_inst(instance);
    if (!I || !msg || len < 1 || source == MOVE_MIDI_SOURCE_TOUCH) return;
    int st = msg[0] & 0xF0;
    int d1 = len > 1 ? msg[1] & 0x7F : 0;
    int d2 = len > 2 ? msg[2] & 0x7F : 0;
    switch (st) {
    case 0x90:
        if (d2 > 0) {
            note_on(I, d1, d2);
            break;
        }
        /* fall through: note-on with velocity 0 is a note-off */
    case 0x80: note_off(I, d1); break;
    case 0xB0:
        switch (d1) {
        case 64:
            I->sustain_down = d2 >= 64;
            if (!I->sustain_down)
                for (int i = 0; i < DRZ_VOICES; i++)
                    if (I->sustained[i]) {
                        I->sustained[i] = 0;
                        if (I->voice[i].gate) release_voice(I, &I->voice[i]);
                    }
            break;
        case 69: { /* Freeze: a rising edge toggles it (plan/05 section 3) */
            int on = d2 >= 64;
            if (on && !I->cc69) params_set(I, "buf_freeze", I->v[P_BUF_FREEZE] >= 0.5f ? "0" : "1");
            I->cc69 = on;
            break;
        }
        case 120: voices_all_off(I, 1); break;
        case 121: I->bend = 0.0f; break;
        case 123: voices_all_off(I, 0); break;
        default: break;
        }
        break;
    case 0xE0: I->bend = (float)(((d2 << 7) | d1) - 8192) * (1.0f / 8192.0f); break;
    default: break;
    }
}

static void drz_set_param(void *instance, const char *key, const char *val) {
    drz_inst_t *I = drz_inst(instance);
    if (I) params_set(I, key, val);
}

static int drz_get_param(void *instance, const char *key, char *buf, int buf_len) {
    drz_inst_t *I = drz_inst(instance);
    if (!I) return -1;
    return params_get(I, key, buf, buf_len);
}

/* ------------------------------------------------------------------ render */

/* Linear below 0.8, then a smooth knee towards 1.0 (Crest's master clipper). */
static inline float soft_clip(float x) {
    float ax = fabsf(x);
    if (ax <= 0.8f) return x;
    float y = 0.8f + 0.2f * fast_tanh((ax - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
}

static void apply_params(drz_inst_t *I) {
    I->pdirty = 0;
    source_prepare(I);
    buffer_t *b = &I->buf;
    int mode = (int)I->v[P_BUF_MODE];
    if (mode != b->mode) buffer_set_mode(I, mode);
    int want = I->v[P_BUF_FREEZE] >= 0.5f;
    if (b->mode == BUF_MODE_LIVE && want != (b->frozen || b->wait_full)) buffer_set_freeze(I, want);
}

static void drz_render_block(void *instance, int16_t *out, int frames) {
    drz_inst_t *I = drz_inst(instance);
    if (!out || frames <= 0) return;
    if (!I) {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
#ifdef DRZ_PROFILE
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
    uint64_t fp = ftz_enter();
    if (I->pdirty) apply_params(I);
    if (g_host && g_host->get_bpm) {
        float bpm = g_host->get_bpm();
        I->bpm = bpm > 20.0f && bpm < 400.0f ? bpm : 120.0f;
    }
    env_par_t ep = {I->v[P_AE_A], I->v[P_AE_D], I->v[P_AE_S], I->v[P_AE_R]};
    env_par_t mp = {I->v[P_ME_A], I->v[P_ME_D], I->v[P_ME_S], I->v[P_ME_R]};
    float gt = glide_time(I), glide_c = gt > 0.0f ? expf(-DRZ_CTL_DT * 3.0f / gt) : 0.0f;
    float scan = map_scan(I->v[P_GR_SCAN]);
    if ((int)I->v[P_GR_PQ] == GR_PQ_SCALE) scale_update(I);
    /* Track Notes (Live): the source plays the newest held key, gliding with the voices */
    if (I->v[P_SRC_TRACK] >= 0.5f && I->nheld > 0) {
        float tgt = (float)I->held[I->nheld - 1] + 12.0f * I->v[P_V_OCT] - I->v[P_SRC_ROOT];
        float c = gt > 0.0f ? expf(-(float)frames / DRZ_FS * 3.0f / gt) : 0.0f;
        I->track_semis = tgt + (I->track_semis - tgt) * c;
    } else if (I->v[P_SRC_TRACK] < 0.5f) {
        I->track_semis = 0.0f;
    }
    float vl[DRZ_VOICES][DRZ_SUB], vr[DRZ_VOICES][DRZ_SUB];

    for (int done = 0; done < frames;) {
        int cnt = frames - done;
        if (cnt > DRZ_MAX_FRAMES) cnt = DRZ_MAX_FRAMES;
        buffer_block(I, cnt); /* the source first: grains read what was written for this chunk */
        /* Grain positions are aligned to the output sample: sample sb of this chunk is absolute w - cnt + sb. */
        int live = I->buf.mode == BUF_MODE_LIVE && !I->buf.frozen;
        int64_t base = live ? I->buf.w - cnt : (int64_t)1 << 60;
        int32_t readable = buffer_readable(I);
        memset(I->busl, 0, (size_t)cnt * sizeof(float));
        memset(I->busr, 0, (size_t)cnt * sizeof(float));
        for (int sb = 0; sb < cnt; sb += DRZ_SUB) {
            int n = cnt - sb < DRZ_SUB ? cnt - sb : DRZ_SUB;
            float env0[DRZ_VOICES];
            memset(vl, 0, sizeof vl);
            memset(vr, 0, sizeof vr);
            for (int vi = 0; vi < DRZ_VOICES; vi++) {
                voice_t *v = &I->voice[vi];
                env0[vi] = v->amp.y;
                if (!v->active) continue;
                voice_control(I, v, &ep, &mp, glide_c);
                if (readable > 0) v->scan_off += scan * (float)n / (float)readable;
                voice_spawn_grains(I, v, vi, n, base + sb);
            }
            grains_render(I, vl, vr, n, base + sb);
            I->clock += n;
            for (int vi = 0; vi < DRZ_VOICES; vi++) {
                voice_t *v = &I->voice[vi];
                if (!v->active) continue;
                voice_filter(I, v, vl[vi], vr[vi], n);
                float k = VOICE_GAIN * v->vel_gain;
                float g = env0[vi] * k, dg = (v->amp.y - env0[vi]) * k / (float)n;
                for (int i = 0; i < n; i++, g += dg) {
                    I->busl[sb + i] += vl[vi][i] * g;
                    I->busr[sb + i] += vr[vi][i] * g;
                }
                if (!v->gate && v->amp.stage == ES_IDLE && v->n_grains == 0) v->active = 0;
            }
        }
        float chk = 0.0f;
        for (int i = 0; i < cnt; i++) chk += I->busl[i] * I->busl[i] + I->busr[i] * I->busr[i];
        if (!isfinite(chk) || chk > 1e12f) { /* NaN guard (plan/03 section 5) */
            memset(I->busl, 0, (size_t)cnt * sizeof(float));
            memset(I->busr, 0, (size_t)cnt * sizeof(float));
            grains_kill_all(I);
            for (int vi = 0; vi < DRZ_VOICES; vi++) {
                filt_reset(&I->voice[vi].fl);
                filt_reset(&I->voice[vi].fr);
            }
            fx_reset(&I->fx);
            source_reset(&I->src_live, (uint32_t)I->v[P_SRC_SEED]);
            I->nan_resets++;
        }
        fx_process(&I->fx, I->v, I->bpm, I->busl, I->busr, cnt);
        float vol = 2.0f * sqrf(I->v[P_PATCH_VOL]), dv = (vol - I->vol_cur) / (float)cnt;
        int16_t *o = out + 2 * done;
        for (int i = 0; i < cnt; i++) {
            I->vol_cur += dv;
            float l = soft_clip(I->busl[i] * I->vol_cur) * 32767.0f;
            float r = soft_clip(I->busr[i] * I->vol_cur) * 32767.0f;
            o[2 * i] = (int16_t)lrintf(clampf(l, -32768.0f, 32767.0f));
            o[2 * i + 1] = (int16_t)lrintf(clampf(r, -32768.0f, 32767.0f));
        }
        I->vol_cur = vol;
        done += cnt;
    }
    ftz_exit(fp);
#ifdef DRZ_PROFILE
    clock_gettime(CLOCK_MONOTONIC, &t1);
    I->cpu_us += 0.05f * (((float)(t1.tv_sec - t0.tv_sec)) * 1e6f + ((float)(t1.tv_nsec - t0.tv_nsec)) * 1e-3f - I->cpu_us);
#endif
}

/* ------------------------------------------------------------------ entry */

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = drz_create_instance,
    .destroy_instance = drz_destroy_instance,
    .on_midi = drz_on_midi,
    .set_param = drz_set_param,
    .get_param = drz_get_param,
    .get_error = drz_get_error,
    .render_block = drz_render_block,
};

DRZ_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
