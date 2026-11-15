/* The shared source (plan/04). M0: one Analog oscillator, Sine or Saw, at the root note.
 * The saw is Crest's 2-point polyBLEP. */
#include "drizzle.h"
#include "dsp_util.h"

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

void source_render(drz_inst_t *I, float *out, int n) {
    float dt = semis_to_hz(I->v[P_SRC_ROOT]) / DRZ_FS;
    float t = I->src.phase;
    if ((int)I->v[P_AN_A_WAVE] == AW_SINE) {
        for (int i = 0; i < n; i++) {
            out[i] = sin_cycle(t);
            t += dt;
            if (t >= 1.0f) t -= 1.0f;
        }
    } else {
        for (int i = 0; i < n; i++) {
            out[i] = (2.0f * t - 1.0f) - 2.0f * blep_res(t, dt);
            t += dt;
            if (t >= 1.0f) t -= 1.0f;
        }
    }
    I->src.phase = t;
}
