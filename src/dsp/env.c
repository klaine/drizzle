/* ADSR envelope: Crest's env.c (MIT, same author) without delay, hold and repeats. */
#include "env.h"

#include <math.h>

#include "dsp_util.h"

float env_time(float x, float tmax) { return 0.0005f * powf(tmax / 0.0005f, clampf(x, 0.0f, 1.0f)); }

float env_release_time(float x) {
    return env_time(powf(clampf(x, 0.0f, 1.0f), ENV_RELEASE_BEND), ENV_RELEASE_MAX);
}

void env_trigger(env_t *e, int hard_reset) {
    if (hard_reset) e->y = 0.0f;
    e->stage = ES_ATTACK;
}

void env_release(env_t *e) {
    if (e->stage == ES_IDLE) return;
    if (e->y < 0.001f) {
        e->y = 0.0f;
        e->stage = ES_IDLE;
        return;
    }
    e->rel_ln = logf((e->y + 0.01f) / 0.01f);
    e->stage = ES_RELEASE;
}

/* Attack aims at 3.0 and hands over at 1.0 (gently convex), decay aims just below sustain, release
 * just below zero; each time constant makes the segment last its knob time whatever the level. */
float env_tick(env_t *e, const env_par_t *p) {
    switch (e->stage) {
    case ES_ATTACK: {
        float c = expf(-0.4055f * DRZ_CTL_DT / env_time(p->a, ENV_ATTACK_MAX));
        e->y = 3.0f + (e->y - 3.0f) * c;
        if (e->y >= 1.0f) {
            e->y = 1.0f;
            e->stage = ES_DECAY;
        }
        break;
    }
    case ES_DECAY: {
        float s = p->s;
        if (e->y <= s + 0.002f) {
            e->y = s;
            e->stage = ES_SUSTAIN;
        } else {
            float ln = logf((1.0f - s + 0.01f) / 0.01f);
            float c = expf(-ln * DRZ_CTL_DT / env_time(p->d, ENV_DECAY_MAX));
            float target = s - 0.01f;
            e->y = target + (e->y - target) * c;
            if (e->y <= s + 0.002f) {
                e->y = s;
                e->stage = ES_SUSTAIN;
            }
        }
        break;
    }
    case ES_SUSTAIN: e->y = p->s; break;
    case ES_RELEASE: {
        float c = expf(-e->rel_ln * DRZ_CTL_DT / env_release_time(p->r));
        e->y = -0.01f + (e->y + 0.01f) * c;
        if (e->y <= 0.0f) {
            e->y = 0.0f;
            e->stage = ES_IDLE;
        }
        break;
    }
    default: break;
    }
    return e->y;
}
