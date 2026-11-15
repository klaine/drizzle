/* Native tests: drive Drizzle through the Schwung plugin API with a fake host (plan/13 section 2).
 * Built with -DDRZ_TEST, which tags every ring sample with its absolute position so the read guard
 * can be checked on every sample a grain reads.
 *
 *   build/drizzle_test [outdir]   outdir: where to write chain_params.json and ui_hierarchy.json */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

static int g_pass, g_fail;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
        } else {                                                           \
            g_fail++;                                                      \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                  \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static plugin_api_v2_t *api;
static host_api_v1_t host;

/* ------------------------------------------------------------------ helpers */

static void *mk(void) { return api->create_instance(".", ""); }
static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }
static void spf(void *h, const char *k, float v) {
    char b[40];
    snprintf(b, sizeof b, "%.6f", (double)v);
    sp(h, k, b);
}
static float gp(void *h, const char *k) {
    char b[64];
    int n = api->get_param(h, k, b, sizeof b);
    return n < 0 ? -9999.0f : strtof(b, NULL);
}
static void midi(void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}
static void note_on(void *h, int n, int v) { midi(h, 0x90, n, v); }
static void note_off(void *h, int n) { midi(h, 0x80, n, 0); }

/* Render `frames` (a multiple of 128) into L and R (either may be NULL). */
static void render(void *h, int frames, float *L, float *R) {
    int16_t blk[256];
    for (int p = 0; p < frames; p += 128) {
        api->render_block(h, blk, 128);
        for (int i = 0; i < 128; i++) {
            if (L) L[p + i] = blk[2 * i] * (1.0f / 32768.0f);
            if (R) R[p + i] = blk[2 * i + 1] * (1.0f / 32768.0f);
        }
    }
}

static float rms(const float *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * (double)x[i];
    return (float)sqrt(s / (n > 0 ? n : 1));
}
static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) p = fmaxf(p, fabsf(x[i]));
    return p;
}
static int all_finite(const float *x, int n) {
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i])) return 0;
    return 1;
}
/* Energy at frequency f (Goertzel). */
static double goertzel(const float *x, int n, double f) {
    double w = 2.0 * 3.14159265358979323846 * f / 44100.0, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double s0 = (double)x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* Energy within +-1.5 % of f: grains of a few hundred ms spread a partial over a few Hz. */
static double band(const float *x, int n, double f) {
    double e = 0;
    for (int k = -6; k <= 6; k++) e += goertzel(x, n, f * (1.0 + 0.0025 * k));
    return e;
}

#define SEC 44032 /* 344 blocks, about 1 s */
static float L[8 * SEC], R[8 * SEC];

/* ------------------------------------------------------------------ suites */

static void test_abi(void) {
    printf("abi & pool\n");
    CHECK(api && api->api_version == 2, "api_version");
    void *h[5];
    for (int i = 0; i < 4; i++) {
        h[i] = mk();
        CHECK(h[i] != NULL, "instance %d", i);
    }
    h[4] = mk();
    CHECK(h[4] == NULL, "a fifth instance must fail");
    char e[80];
    CHECK(api->get_error(NULL, e, sizeof e) > 0 && strstr(e, "in use"), "pool-full error text");
    api->destroy_instance(h[2]);
    void *again = mk();
    CHECK(again == h[2], "a released slot is reused");
    for (int i = 0; i < 4; i++) api->destroy_instance(i == 2 ? again : h[i]);
    int16_t blk[256];
    api->render_block(NULL, blk, 128); /* no crash on a bad handle */
    CHECK(api->get_param(h[0], "state", e, sizeof e) < 0, "a destroyed instance answers nothing");
}

static void test_params(void) {
    printf("params\n");
    void *h = mk();
    for (int i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = &DRZ_PARAMS[i];
        float vals[3] = {d->min, d->max, d->def};
        for (int k = 0; k < 3; k++) {
            spf(h, d->key, vals[k]);
            CHECK(fabsf(gp(h, d->key) - vals[k]) < 1e-4f, "%s round trip %g", d->key, (double)vals[k]);
        }
        spf(h, d->key, d->max + 10.0f);
        CHECK(fabsf(gp(h, d->key) - d->max) < 1e-4f, "%s clamps", d->key);
        spf(h, d->key, d->def);
    }
    sp(h, "an_a_wave", "Sine");
    CHECK(gp(h, "an_a_wave") == AW_SINE, "enum by name");
    sp(h, "gr_pos", "0.25");
    sp(h, "gr_pos", "nan");
    sp(h, "gr_pos", "inf");
    sp(h, "gr_pos", "rubbish");
    CHECK(fabsf(gp(h, "gr_pos") - 0.25f) < 1e-5f, "nan, inf and junk are ignored");
    sp(h, "no_such_key", "1");
    CHECK(api->get_param(h, "no_such_key", (char[8]){0}, 8) < 0, "unknown keys answer -1");
    char small[8];
    CHECK(api->get_param(h, "chain_params", small, sizeof small) < 0, "a short buffer gets -1, not a cut string");
    api->destroy_instance(h);
}

static void test_state(void) {
    printf("state\n");
    void *a = mk(), *b = mk();
    static char s1[8192], s2[8192];
    uint32_t rng = 12345;
    int same = 1;
    for (int t = 0; t < 300; t++) {
        for (int i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = &DRZ_PARAMS[i];
            spf(a, d->key, d->min + (d->max - d->min) * rng_uni(&rng));
        }
        int n1 = api->get_param(a, "state", s1, sizeof s1);
        sp(b, "state", s1);
        int n2 = api->get_param(b, "state", s2, sizeof s2);
        if (n1 <= 0 || n1 != n2 || strcmp(s1, s2) != 0) same = 0;
    }
    CHECK(same, "300 random patches round-trip byte-identically");
    printf("  state blob: %d bytes\n", (int)strlen(s1));
    spf(a, "gr_pos", 0.123);
    api->get_param(a, "state", s1, sizeof s1);
    sp(b, "state", "{\"fmt\":\"crest\",\"v\":\"1\",\"gr_pos\":\"0.9\"}");
    CHECK(fabsf(gp(b, "gr_pos") - 0.9f) > 0.01f, "another module's blob is ignored");
    sp(b, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"name\":\"Wet \\\"Cloud\\\"\",\"gr_pos\":\"0.9\"}");
    CHECK(fabsf(gp(b, "gr_pos") - 0.9f) < 1e-5f && fabsf(gp(b, "gr_size") - 0.55f) < 1e-5f,
          "missing keys fall back to INIT");
    char nm[32];
    api->get_param(b, "name", nm, sizeof nm);
    CHECK(strcmp(nm, "Wet \"Cloud\"") == 0, "escaped name: %s", nm);
    api->get_param(b, "state", s2, sizeof s2);
    sp(a, "state", s2);
    api->get_param(a, "name", nm, sizeof nm);
    CHECK(strcmp(nm, "Wet \"Cloud\"") == 0, "name survives a second round trip");
    /* malformed blobs never crash and never read past the string */
    const char *bad[] = {"", "{", "{\"fmt\":\"drizzle\"", "{\"fmt\":\"drizzle\",\"gr_pos\":", "{\"fmt\":\"drizzle\",\"gr_pos\":\"0.3",
                         "garbage \"\"\"\" ::: \"", "{\"fmt\":\"drizzle\",\"name\":\"\\"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) sp(b, "state", bad[i]);
    static char huge[100000];
    memset(huge, '"', sizeof huge - 1);
    sp(b, "state", huge);
    render(b, 1280, L, R);
    CHECK(all_finite(L, 1280), "still renders after malformed blobs");
    CHECK(api->get_param(a, "state", s1, 64) < 0, "a short state buffer gets -1");
    api->destroy_instance(a);
    api->destroy_instance(b);
}

static void test_render(void) {
    printf("render\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    render(h, SEC / 2, L, R); /* the ring fills */
    CHECK(peak(L, SEC / 2) == 0.0f, "silent without notes");
    note_on(h, 48, 100);
    render(h, 2 * SEC, L, R);
    float r1 = rms(L + SEC, SEC);
    CHECK(r1 > 0.01f && r1 < 0.5f, "a held note sounds (rms %.3f)", (double)r1);
    CHECK(all_finite(L, 2 * SEC) && all_finite(R, 2 * SEC), "finite");
    CHECK(fabsf(rms(L + SEC, SEC) - rms(R + SEC, SEC)) < 0.5f * r1, "both channels carry the cloud");
    note_off(h, 48);
    render(h, 4 * SEC, L, R);
    CHECK(peak(L + 3 * SEC, SEC) == 0.0f, "silent after the release");
    CHECK(!I->voice[0].active && grains_active(I) == 0, "voice and grains are freed");
    CHECK(I->guard_violations == 0, "guard: %u violations", I->guard_violations);
    CHECK(I->nan_resets == 0, "no NaN resets");

    /* chord, then CC 120: silent at once */
    for (int k = 0; k < 4; k++) note_on(h, 48 + 3 * k, 100);
    render(h, SEC, L, R);
    midi(h, 0xB0, 120, 0);
    render(h, 1280, L, R);
    CHECK(peak(L + 256, 1024) == 0.0f, "CC 120 silences within 256 samples");

    /* sustain holds note-offs */
    note_on(h, 50, 100);
    midi(h, 0xB0, 64, 127);
    note_off(h, 50);
    render(h, SEC, L, R);
    CHECK(rms(L + SEC / 2, SEC / 2) > 0.01f, "sustain holds the note");
    midi(h, 0xB0, 64, 0);
    render(h, 4 * SEC, L, R);
    CHECK(peak(L + 3 * SEC, SEC) == 0.0f, "pedal up releases it");

    /* stealing: nine notes, never more than 8 voices, never more than 128 grains */
    for (int k = 0; k < 9; k++) note_on(h, 40 + k, 100);
    int over = 0;
    for (int b = 0; b < 400; b++) {
        render(h, 128, L, R);
        if (grains_active(I) > DRZ_GRAINS) over = 1;
    }
    CHECK(!over, "pool limit");
    int act = 0;
    for (int v = 0; v < DRZ_VOICES; v++) act += I->voice[v].active;
    CHECK(act == 8, "8 voices after 9 notes (%d)", act);
    CHECK(I->guard_violations == 0, "guard after stealing: %u", I->guard_violations);
    api->destroy_instance(h);
}

static void test_pitch(void) {
    printf("pitch\n");
    void *h = mk();
    sp(h, "an_a_wave", "Sine");
    spf(h, "gr_spray", 0.0);
    spf(h, "gr_size", 0.7); /* about 330 ms */
    spf(h, "gr_dens", 0.6); /* 21 grains/s, overlap 7 */
    render(h, SEC, NULL, NULL);
    int notes[] = {48, 55, 60, 67, 72};
    for (size_t k = 0; k < sizeof notes / sizeof *notes; k++) {
        note_on(h, notes[k], 100);
        render(h, 4 * SEC, L, R);
        double f = 440.0 * pow(2.0, (notes[k] - 69) / 12.0);
        double on = band(L + SEC, 3 * SEC, f);
        double lo = band(L + SEC, 3 * SEC, f * pow(2.0, -1.0 / 12.0)), hi = band(L + SEC, 3 * SEC, f * pow(2.0, 1.0 / 12.0));
        CHECK(on > 100.0 * lo && on > 100.0 * hi, "note %d: energy at %.1f Hz is 20 dB above its neighbours", notes[k], f);
        note_off(h, notes[k]);
        render(h, 4 * SEC, NULL, NULL);
    }
    api->destroy_instance(h);
}

static void test_level(void) {
    printf("level\n");
    void *h = mk();
    spf(h, "gr_size", 0.5); /* about 100 ms */
    spf(h, "gr_spray", 0.4);
    spf(h, "gr_jit", 1.0);
    render(h, 4 * SEC, NULL, NULL);
    float dens[] = {0.5f, 0.75f, 1.0f}; /* 11, 53, 250 grains/s: overlap 1.1 .. 25 */
    float lv[3];
    for (int k = 0; k < 3; k++) {
        spf(h, "gr_dens", dens[k]);
        note_on(h, 48, 100);
        render(h, 4 * SEC, L, NULL);
        lv[k] = rms(L + SEC, 3 * SEC);
        note_off(h, 48);
        render(h, 4 * SEC, NULL, NULL);
    }
    for (int k = 1; k < 3; k++)
        CHECK(fabsf(20.0f * log10f(lv[k] / lv[0])) < 3.0f, "density %.2f: %.2f dB from density 0.5", (double)dens[k],
              (double)(20.0f * log10f(lv[k] / lv[0])));
    api->destroy_instance(h);
}

/* Random positions, sizes, pitches, buffer lengths and freezes: no grain may read a sample that was
 * not written at the position it means, or one written after "now" (plan/06 section 8). */
static void test_guard_fuzz(void) {
    printf("guard fuzz\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    uint32_t rng = 777;
    for (int round = 0; round < 60; round++) {
        spf(h, "gr_pos", rng_uni(&rng));
        spf(h, "gr_spray", rng_uni(&rng));
        spf(h, "gr_size", rng_uni(&rng));
        spf(h, "gr_dens", rng_uni(&rng));
        spf(h, "gr_jit", rng_uni(&rng));
        spf(h, "buf_len", rng_uni(&rng));
        spf(h, "src_root", 24 + (int)(72 * rng_uni(&rng)));
        if (rng_uni(&rng) < 0.15f) sp(h, "buf_freeze", I->v[P_BUF_FREEZE] >= 0.5f ? "0" : "1");
        if (rng_uni(&rng) < 0.05f) sp(h, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\"}"); /* the ring starts over */
        int note = 24 + (int)(72 * rng_uni(&rng));
        note_on(h, note, 100);
        render(h, (int)(SEC / 4 + SEC * rng_uni(&rng)) & ~127, NULL, NULL);
        if (rng_uni(&rng) < 0.6f) note_off(h, note);
    }
    CHECK(I->guard_violations == 0, "%u guard violations", I->guard_violations);
    CHECK(I->nan_resets == 0, "no NaN resets");
    printf("  %u onsets skipped (pool full, voice cap or too little buffer)\n", I->grains_skipped);
    api->destroy_instance(h);
}

static void test_freeze(void) {
    printf("freeze\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    sp(h, "an_a_wave", "Sine");
    spf(h, "buf_len", 0.25); /* 1 s */
    render(h, 2 * SEC, NULL, NULL);
    sp(h, "buf_freeze", "On");
    render(h, 1280, NULL, NULL);
    const buffer_t *b = &I->buf;
    CHECK(b->frozen && b->xf_k == DRZ_XFADE, "frozen, seam crossfade done");
    CHECK(b->lf == buffer_len_samples(I), "the loop is buf_len long (%d)", b->lf);
    /* the loop's end runs on into its start: the step there is a sine step, not a jump */
    const float *d = I->data[0];
    int32_t last = (b->ls + b->lf - 1) % DRZ_RING, first = b->ls;
    float seam = fabsf(d[first] - d[last]);
    float step = 2.0f * DRZ_PI * semis_to_hz(48) / DRZ_FS;
    CHECK(seam < 1.5f * step, "seam step %.4f (a sine step is %.4f)", (double)seam, (double)step);
    float worst = 0;
    for (int k = 0; k < DRZ_XFADE + 8; k++) {
        int32_t i0 = (b->ls + k) % DRZ_RING, i1 = (i0 + 1) % DRZ_RING;
        worst = fmaxf(worst, fabsf(d[i1] - d[i0]));
    }
    CHECK(worst < 1.5f * step, "no step larger than a sine step through the crossfade (%.4f)", (double)worst);

    int64_t w0 = b->w;
    note_on(h, 48, 100);
    render(h, 8 * SEC, L, NULL);
    CHECK(b->w == w0, "nothing is written while frozen");
    CHECK(rms(L + 7 * SEC, SEC) > 0.01f, "a frozen buffer keeps sounding");
    sp(h, "buf_freeze", "Off");
    render(h, SEC, NULL, NULL);
    CHECK(!b->frozen && b->w > w0, "unfreeze resumes writing");

    /* CC 69 toggles */
    midi(h, 0xB0, 69, 127);
    render(h, 128, NULL, NULL);
    CHECK(b->frozen && gp(h, "buf_freeze") == 1.0f, "CC 69 freezes");
    midi(h, 0xB0, 69, 127);
    render(h, 128, NULL, NULL);
    CHECK(b->frozen, "no toggle without a release in between");
    midi(h, 0xB0, 69, 0);
    midi(h, 0xB0, 69, 127);
    render(h, 128, NULL, NULL);
    CHECK(!b->frozen, "the next press unfreezes");

    /* a saved freeze waits until the ring holds buf_len again */
    static char s[8192];
    sp(h, "buf_freeze", "On");
    render(h, 1280, NULL, NULL);
    api->get_param(h, "state", s, sizeof s);
    sp(h, "state", s);
    CHECK(!b->frozen && b->wait_full, "after a load the freeze waits");
    render(h, SEC / 2, NULL, NULL);
    CHECK(!b->frozen, "still waiting at 0.5 s");
    render(h, SEC, NULL, NULL);
    CHECK(b->frozen && b->lf == buffer_len_samples(I), "frozen once 1 s is recorded");
    CHECK(I->guard_violations == 0, "guard: %u", I->guard_violations);
    api->destroy_instance(h);
}

static void test_pool(void) {
    printf("pool\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    spf(h, "gr_dens", 1.0);
    spf(h, "gr_size", 1.0);
    spf(h, "buf_len", 1.0);
    render(h, 9 * SEC, NULL, NULL);
    for (int k = 0; k < 8; k++) note_on(h, 40 + 3 * k, 100);
    int maxg = 0, cap_ok = 1;
    for (int b = 0; b < 3 * 344; b++) {
        render(h, 128, L, R);
        if (grains_active(I) > maxg) maxg = grains_active(I);
        for (int v = 0; v < DRZ_VOICES; v++)
            if (I->voice[v].n_grains > DRZ_VOICE_GRAINS) cap_ok = 0;
    }
    CHECK(maxg == DRZ_GRAINS, "the pool fills (%d) and never overflows", maxg);
    CHECK(cap_ok, "no voice above its cap");
    CHECK(I->grains_skipped > 0, "onsets are skipped, not stolen");
    CHECK(all_finite(L, 128) && I->nan_resets == 0, "finite");
    int sum = 0;
    for (int v = 0; v < DRZ_VOICES; v++) sum += I->voice[v].n_grains;
    CHECK(sum == grains_active(I), "per-voice counts add up (%d vs %d)", sum, grains_active(I));
    api->destroy_instance(h);
}

static void dump(const char *dir, const char *key) {
    static char buf[32768];
    void *h = mk();
    int n = api->get_param(h, key, buf, sizeof buf);
    api->destroy_instance(h);
    CHECK(n > 0, "%s answered", key);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.json", dir, key);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "write %s", path);
    if (!f) return;
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
}

int main(int argc, char **argv) {
    memset(&host, 0, sizeof host);
    api = move_plugin_init_v2(&host);
    test_abi();
    test_params();
    test_state();
    test_render();
    test_pitch();
    test_level();
    test_guard_fuzz();
    test_freeze();
    test_pool();
    if (argc > 1) {
        dump(argv[1], "chain_params");
        dump(argv[1], "ui_hierarchy");
    }
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
