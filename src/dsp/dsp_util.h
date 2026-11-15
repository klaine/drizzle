/* Small realtime-safe helpers shared by the DSP sources (plan/03 section 6). Taken from Crest. */
#ifndef DRZ_DSP_UTIL_H
#define DRZ_DSP_UTIL_H

#include <math.h>
#include <stdint.h>

#define DRZ_FS 44100.0f
#define DRZ_TAU 6.28318530717958647692f
#define DRZ_PI 3.14159265358979323846f

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float sqrf(float x) { return x * x; }

/* Rational (Pade) tanh approximation, exactly +-1 at |x| = 3 (Amino's am_tanh, Crest's fast_tanh). */
static inline float fast_tanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline uint32_t rng_u32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x1234567u;
    return *s;
}
/* Uniform in [0, 1). */
static inline float rng_uni(uint32_t *s) { return (float)(rng_u32(s) & 0xFFFFFFu) * (1.0f / 16777216.0f); }
/* Uniform in [-1, 1). */
static inline float rng_bi(uint32_t *s) { return rng_uni(s) * 2.0f - 1.0f; }
/* Triangular in (-1, 1): most values near 0 (grain spray, plan/06 section 3). */
static inline float rng_tri(uint32_t *s) { return rng_uni(s) - rng_uni(s); }

/* sin(2 pi t) for t in cycles, folded onto a quarter cycle: a 9th-order series within 4e-6 of sinf. */
static inline float sin_cycle(float t) {
    float x = t - floorf(t);
    if (x >= 0.5f) x -= 1.0f;
    if (x > 0.25f) x = 0.5f - x;
    else if (x < -0.25f) x = -0.5f - x;
    float u = DRZ_TAU * x, u2 = u * u;
    return u * (1.0f + u2 * (-1.6666667e-1f + u2 * (8.3333333e-3f + u2 * (-1.9841270e-4f + u2 * 2.7557319e-6f))));
}

/* Display brightness of a stretch of audio, 0..255 (plan/05 section 5): from the energy of its first
 * difference against its energy (for a sine at f that ratio is 2 sin(pi f / fs)), the frequency the
 * stretch mostly sits at, on a log scale from 50 Hz (0) to 10 kHz (255). Silence reads 0. */
static inline uint8_t bright_byte(float energy, float diff_energy) {
    if (energy < 1e-7f) return 0;
    float r = sqrtf(diff_energy / energy);
    if (r > 1.999f) r = 1.999f;
    float hz = 44100.0f / 3.14159265f * asinf(0.5f * r);
    float b = logf(hz / 50.0f) / logf(200.0f);
    return (uint8_t)(255.0f * (b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b)));
}

static inline float semis_to_hz(float semis) { return 440.0f * exp2f((semis - 69.0f) / 12.0f); }

/* 4-point, 3rd-order Hermite interpolation between x0 and x1 (xm1, x2 the outer neighbours). */
static inline float hermite4(float xm1, float x0, float x1, float x2, float t) {
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

/* Flush denormals to zero for the duration of render_block (aarch64 FPCR.FZ). */
#if defined(__aarch64__)
static inline uint64_t ftz_enter(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v));
    __asm__ volatile("msr fpcr, %0" : : "r"(v | (1ull << 24)));
    return v;
}
static inline void ftz_exit(uint64_t v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
#else
static inline uint64_t ftz_enter(void) { return 0; }
static inline void ftz_exit(uint64_t v) { (void)v; }
#endif

#endif
