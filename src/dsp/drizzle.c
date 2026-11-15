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

static void note_on(drz_inst_t *I, int note, int vel) {
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
    int vi = (int)(pick - I->voice);
    if (pick->active && pick->note != note) voice_kill_grains(I, vi); /* a stolen voice's grains fade in 64 samples */
    if (!pick->active) {
        pick->amp.y = 0.0f;
        pick->amp.stage = ES_IDLE;
    }
    if (I->keys_down == 0) I->src_live.sweep_s = 0.0f; /* the Live sweep restarts with a new phrase */
    I->keys_down++;
    pick->active = 1;
    pick->gate = 1;
    pick->note = (uint8_t)note;
    pick->vel = (uint8_t)vel;
    float vn = (float)vel * (1.0f / 127.0f);
    pick->vel_gain = 1.0f - I->v[P_AE_VEL] * (1.0f - vn * vn);
    pick->age = ++I->age_ctr;
    pick->next = 0.0f; /* the first grain starts at note-on */
    pick->scan_off = 0.0f;
    env_trigger(&pick->amp, 0);
    I->sustained[vi] = 0;
}

static void release_voice(drz_inst_t *I, voice_t *v) {
    v->gate = 0;
    env_release(&v->amp);
    if (I->keys_down > 0) I->keys_down--;
}

static void note_off(drz_inst_t *I, int note) {
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
            v->amp.y = 0.0f;
            v->amp.stage = ES_IDLE;
        }
    }
    I->keys_down = 0;
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
                        release_voice(I, &I->voice[i]);
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
    float scan = map_scan(I->v[P_GR_SCAN]);
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
                env_tick(&v->amp, &ep);
                if (readable > 0) v->scan_off += scan * (float)n / (float)readable;
                voice_spawn_grains(I, v, vi, n, base + sb);
            }
            grains_render(I, vl, vr, n, base + sb);
            for (int vi = 0; vi < DRZ_VOICES; vi++) {
                voice_t *v = &I->voice[vi];
                if (!v->active) continue;
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
