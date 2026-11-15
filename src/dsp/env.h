/* ADSR envelope (plan/08 section 1), ticked at control rate (16 samples). Crest's env.c without the
 * delay, hold and repeat stages: the same knob laws and analog-style one-pole segments. */
#ifndef DRZ_ENV_H
#define DRZ_ENV_H

#include <stdint.h>

#define DRZ_CTL_DT (16.0f / 44100.0f)

enum { ES_IDLE = 0, ES_ATTACK, ES_DECAY, ES_SUSTAIN, ES_RELEASE };

#define ENV_ATTACK_MAX 20.0f
#define ENV_DECAY_MAX 22.0f
#define ENV_RELEASE_MAX 25.0f
#define ENV_RELEASE_BEND 0.6f

typedef struct {
    uint8_t stage;
    float y;      /* level 0..1 */
    float rel_ln; /* ln ratio fixing the release curve */
} env_t;

/* Normalised (0..1) knob values. */
typedef struct {
    float a, d, s, r;
} env_par_t;

float env_time(float x, float tmax);  /* 0..1 -> 0.5 ms .. tmax, exponential */
float env_release_time(float x);      /* the same, bent by x^0.6 */
void env_trigger(env_t *e, int hard_reset);
void env_release(env_t *e);
float env_tick(env_t *e, const env_par_t *p);

#endif
