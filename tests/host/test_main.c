/* Native tests: drive Drizzle through the Schwung plugin API with a fake host (plan/13 section 2).
 * Built with -DDRZ_TEST, which tags every Live ring sample with its absolute position so the read
 * guard is checked on every point a grain reads, and counts tape grains that would read past a tape's
 * ends. -DDRZ_TEST_SLOW runs every grain on the wrapping read path.
 *
 *   build/drizzle_test */
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
static float g_bpm = 120.0f;
static float fake_bpm(void) { return g_bpm; }
/* a host transport: running or not, its beat advanced by render() */
static int g_run;
static double g_beat;
static int fake_clock(void) { return g_run ? MOVE_CLOCK_STATUS_RUNNING : MOVE_CLOCK_STATUS_STOPPED; }
static double fake_beat(void) { return g_run ? g_beat : -1.0; }

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
        g_beat += 128.0 * (double)g_bpm / (60.0 * 44100.0);
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
/* Rising zero crossings per second of x (a pitch estimate for clean periodic material). */
static float zc_hz(const float *x, int n) {
    int c = 0;
    for (int i = 1; i < n; i++) c += x[i - 1] < 0.0f && x[i] >= 0.0f;
    return (float)c * 44100.0f / (float)n;
}

/* A plain Live patch with the effects off: the M0 tests' setting. */
static void plain_live(void *h) {
    sp(h, "src_type", "Analog");
    sp(h, "v_flav", "0");
    sp(h, "buf_mode", "Live");
    sp(h, "rev_lvl", "0");
    sp(h, "sw_timb", "0");
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
    api->render_block(NULL, blk, 128);
    CHECK(api->get_param(h[0], "state", e, sizeof e) < 0, "a destroyed instance answers nothing");
}

static void test_params(void) {
    printf("params\n");
    void *h = mk();
    int bad = 0;
    for (int i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = &DRZ_PARAMS[i];
        if (i == P_MM_CLEAR) continue; /* a trigger: it reads back as 0 */
        float vals[3] = {d->mn, d->mx, d->def};
        for (int k = 0; k < 3; k++) {
            spf(h, d->key, vals[k]);
            if (fabsf(gp(h, d->key) - vals[k]) > 1e-4f) bad++;
        }
        spf(h, d->key, d->mx + 10.0f);
        if (fabsf(gp(h, d->key) - d->mx) > 1e-4f) bad++;
        spf(h, d->key, d->def);
    }
    CHECK(bad == 0, "%d of %d keys fail min/max/default/clamp round trips", bad, P_COUNT);
    for (int i = 1; i < P_COUNT; i++) {
        int a = DRZ_PARAMS_SORTED[i - 1], b = DRZ_PARAMS_SORTED[i];
        if (strcmp(DRZ_PARAMS[a].key, DRZ_PARAMS[b].key) >= 0) bad++;
    }
    CHECK(bad == 0, "the sorted key order is strictly increasing");
    for (int i = 0; i < P_COUNT; i++)
        if (params_find(DRZ_PARAMS[i].key) != i) bad++;
    CHECK(bad == 0, "every key is found");
    sp(h, "an_a_wave", "Sine");
    CHECK(gp(h, "an_a_wave") == AN_A_WAVE_SINE, "enum by name");
    sp(h, "gr_pos", "0.25");
    sp(h, "gr_pos", "nan");
    sp(h, "gr_pos", "inf");
    sp(h, "gr_pos", "rubbish");
    CHECK(fabsf(gp(h, "gr_pos") - 0.25f) < 1e-5f, "nan, inf and junk are ignored");
    sp(h, "no_such_key", "1");
    CHECK(api->get_param(h, "no_such_key", (char[8]){0}, 8) < 0, "unknown keys answer -1");
    char small[8];
    CHECK(api->get_param(h, "chain_params", small, sizeof small) < 0, "a short buffer gets -1, not a cut string");
    CHECK(fabsf(gp(h, "gr_pos:effective") - 0.25f) < 1e-5f, "gr_pos:effective answers");
    CHECK(api->get_param(h, "gr_jit:effective", small, sizeof small) < 0, "only live keys have :effective");
    static char cp[32768];
    int n = api->get_param(h, "chain_params", cp, sizeof cp);
    CHECK(n > 1000 && n < 31 * 1024 && cp[0] == '[', "chain_params %d B", n);
    n = api->get_param(h, "ui_hierarchy", cp, sizeof cp);
    CHECK(n > 100 && strncmp(cp, "{\"levels\":", 10) == 0, "ui_hierarchy");
    api->destroy_instance(h);
}

static void test_state(void) {
    printf("state\n");
    void *a = mk(), *b = mk();
    static char s1[16384], s2[16384];
    uint32_t rng = 12345;
    int same = 1;
    for (int t = 0; t < 200; t++) {
        for (int i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = &DRZ_PARAMS[i];
            spf(a, d->key, d->mn + (d->mx - d->mn) * rng_uni(&rng));
        }
        int n1 = api->get_param(a, "state", s1, sizeof s1);
        sp(b, "state", s1);
        int n2 = api->get_param(b, "state", s2, sizeof s2);
        if (n1 <= 0 || n1 != n2 || strcmp(s1, s2) != 0) same = 0;
        render(b, 128, NULL, NULL); /* every random patch renders */
    }
    CHECK(same, "200 random patches round-trip byte-identically");
    printf("  state blob: %d bytes\n", (int)strlen(s1));
    CHECK(strlen(s1) < 8192, "the blob is under 8 KB");
    sp(b, "state", "{\"fmt\":\"crest\",\"v\":\"1\",\"gr_pos\":\"0.9\"}");
    CHECK(fabsf(gp(b, "gr_pos") - 0.9f) > 0.01f, "another module's blob is ignored");
    sp(b, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"name\":\"Wet \\\"Cloud\\\"\",\"gr_pos\":\"0.9\"}");
    CHECK(fabsf(gp(b, "gr_pos") - 0.9f) < 1e-5f && fabsf(gp(b, "gr_size") - 0.55f) < 1e-5f,
          "missing keys fall back to INIT");
    char nm[32];
    api->get_param(b, "name", nm, sizeof nm);
    CHECK(strcmp(nm, "Wet \"Cloud\"") == 0, "escaped name: %s", nm);
    CHECK(gp(b, "preset") == -1.0f, "a state load is not a factory patch");
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

static void test_presets(void) {
    printf("presets & browser\n");
    void *h = mk();
    CHECK(gp(h, "preset_count") >= 1.0f && gp(h, "preset") == 0.0f, "INIT is factory patch 0");
    char b[512];
    CHECK(api->get_param(h, "preset_name", b, sizeof b) > 0 && strcmp(b, "Init") == 0, "preset_name %s", b);
    CHECK(api->get_param(h, "pb_cats", b, sizeof b) > 0 && strstr(b, "All ("), "pb_cats %s", b);
    CHECK(gp(h, "pb_count") == gp(h, "preset_count"), "All lists every patch");
    spf(h, "gr_pos", 0.9f);
    sp(h, "preset", "0");
    CHECK(fabsf(gp(h, "gr_pos") - 0.5f) < 1e-5f, "loading a patch restores its values");
    sp(h, "pb_cat", "1");
    CHECK(gp(h, "pb_cat") == 1.0f && gp(h, "pb_count") >= 1.0f, "a category");
    sp(h, "pb_index", "0");
    CHECK(gp(h, "pb_index") == 0.0f, "pb_index");
    /* every factory patch loads, plays a chord and stays finite */
    int n = (int)gp(h, "preset_count"), quiet = 0, bad = 0;
    for (int i = 0; i < n; i++) {
        char idx[8];
        snprintf(idx, sizeof idx, "%d", i);
        sp(h, "preset", idx);
        render(h, SEC, NULL, NULL); /* the tape or ring fills */
        for (int k = 0; k < 3; k++) note_on(h, 48 + 3 * k + (k == 2), 100);
        render(h, 2 * SEC, L, R);
        if (!all_finite(L, 2 * SEC) || !all_finite(R, 2 * SEC) || peak(L, 2 * SEC) >= 0.99f) bad++;
        if (rms(L + SEC, SEC) < 0.01f) quiet++;
        midi(h, 0xB0, 120, 0);
        render(h, 1280, NULL, NULL);
    }
    CHECK(n >= 7 && bad == 0 && quiet == 0, "%d patches: %d not finite or clipping, %d silent", n, bad, quiet);
    api->destroy_instance(h);
}

/* The Analog source on its own: every wave, sync, FM, density, noise, fold and tone stay finite and
 * bounded, and the sine sits at the root. */
static void test_source(void) {
    printf("source\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    static float buf[SEC];
    int ok = 1;
    float worst = 0;
    uint32_t rng = 99;
    for (int t = 0; t < 120; t++) {
        spf(h, "an_a_wave", (float)(t % 4));
        spf(h, "an_b_wave", (float)((t / 4) % 4));
        spf(h, "an_a_shape", rng_uni(&rng));
        spf(h, "an_b_shape", rng_uni(&rng));
        spf(h, "an_b_lvl", rng_uni(&rng));
        spf(h, "an_b_semi", (float)((int)(rng_uni(&rng) * 48) - 24));
        spf(h, "an_dens", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        spf(h, "an_fm", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        spf(h, "an_sync", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        spf(h, "an_noise", rng_uni(&rng) < 0.7f ? 0.0f : rng_uni(&rng));
        spf(h, "src_fold", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        spf(h, "src_tone", rng_uni(&rng) < 0.5f ? 1.0f : rng_uni(&rng));
        spf(h, "src_root", 24.0f + 72.0f * rng_uni(&rng));
        source_prepare(I);
        source_t s;
        source_reset(&s, 1);
        source_render(&I->sp, &s, rng_uni(&rng), 0.0f, buf, 4096);
        if (!all_finite(buf, 4096)) ok = 0;
        worst = fmaxf(worst, peak(buf, 4096));
    }
    CHECK(ok, "finite for every combination");
    CHECK(worst < 2.5f, "bounded (peak %.2f)", (double)worst);
    sp(h, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"src_type\":\"Analog\",\"an_a_wave\":\"Sine\",\"sw_timb\":\"0\"}");
    source_prepare(I);
    source_t s;
    source_reset(&s, 1);
    source_render(&I->sp, &s, 0.0f, 0.0f, buf, SEC);
    float f = zc_hz(buf, SEC);
    CHECK(fabsf(f - semis_to_hz(48)) < 1.5f, "the sine plays the root: %.1f Hz", (double)f);
    double dc = 0;
    for (int i = 0; i < SEC; i++) dc += (double)buf[i];
    CHECK(fabs(dc / SEC) < 0.01, "no DC");
    /* sync: a synced saw keeps the master's pitch */
    sp(h, "an_a_wave", "Saw");
    sp(h, "an_sync", "0.5");
    source_prepare(I);
    source_reset(&s, 1);
    source_render(&I->sp, &s, 0.0f, 0.0f, buf, SEC);
    double f48 = (double)semis_to_hz(48), fund = goertzel(buf, SEC, f48), off = goertzel(buf, SEC, f48 * 1.5);
    CHECK(fund > 10.0 * off, "a synced saw keeps the master pitch");
    api->destroy_instance(h);
}

static void test_render(void) {
    printf("render (Live)\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    plain_live(h);
    render(h, SEC / 2, L, R);
    CHECK(peak(L, SEC / 2) == 0.0f, "silent without notes");
    note_on(h, 48, 100);
    render(h, 2 * SEC, L, R);
    float r1 = rms(L + SEC, SEC);
    CHECK(r1 > 0.01f && r1 < 0.5f, "a held note sounds (rms %.3f)", (double)r1);
    CHECK(all_finite(L, 2 * SEC) && all_finite(R, 2 * SEC), "finite");
    note_off(h, 48);
    render(h, 4 * SEC, L, R);
    CHECK(peak(L + 3 * SEC, SEC) == 0.0f, "silent after the release");
    CHECK(!I->voice[0].active && grains_active(I) == 0, "voice and grains are freed");

    for (int k = 0; k < 4; k++) note_on(h, 48 + 3 * k, 100);
    render(h, SEC, L, R);
    midi(h, 0xB0, 120, 0);
    render(h, 1280, L, R);
    CHECK(peak(L + 256, 1024) == 0.0f, "CC 120 silences within 256 samples");

    note_on(h, 50, 100);
    midi(h, 0xB0, 64, 127);
    note_off(h, 50);
    render(h, SEC, L, R);
    CHECK(rms(L + SEC / 2, SEC / 2) > 0.01f, "sustain holds the note");
    midi(h, 0xB0, 64, 0);
    render(h, 4 * SEC, L, R);
    CHECK(peak(L + 3 * SEC, SEC) == 0.0f, "pedal up releases it");

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
    midi(h, 0xB0, 120, 0);
    render(h, SEC, NULL, NULL);
    sp(h, "v_voices", "3");
    for (int k = 0; k < 5; k++) note_on(h, 40 + k, 100);
    render(h, 1280, NULL, NULL);
    act = 0;
    for (int v = 0; v < DRZ_VOICES; v++) act += I->voice[v].active;
    CHECK(act == 3, "v_voices 3 keeps 3 voices (%d)", act);
    CHECK(I->guard_violations == 0, "guard: %u", I->guard_violations);
    api->destroy_instance(h);
}

static void test_pitch(void) {
    printf("pitch\n");
    void *h = mk();
    plain_live(h);
    sp(h, "an_a_wave", "Sine");
    spf(h, "gr_spray", 0.0f);
    spf(h, "gr_size", 0.7f);
    spf(h, "gr_dens", 0.6f);
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
    /* octave pitch spray lands on octaves, not between them */
    spf(h, "gr_pspr", 12.0f);
    sp(h, "gr_pq", "Oct");
    note_on(h, 60, 100);
    render(h, 4 * SEC, L, R);
    double f = (double)semis_to_hz(60);
    double oct = band(L + SEC, 3 * SEC, f) + band(L + SEC, 3 * SEC, 2 * f) + band(L + SEC, 3 * SEC, f / 2);
    double mid = band(L + SEC, 3 * SEC, f * pow(2.0, 5.0 / 12.0)) + band(L + SEC, 3 * SEC, f * pow(2.0, 7.0 / 12.0));
    CHECK(oct > 100.0 * mid, "Oct spray stays on octaves");
    note_off(h, 60);
    render(h, 3 * SEC, NULL, NULL);
    sp(h, "gr_pq", "Fifth");
    note_on(h, 60, 100);
    render(h, 4 * SEC, L, R);
    double fifth = band(L + SEC, 3 * SEC, f * pow(2.0, 7.0 / 12.0)) + band(L + SEC, 3 * SEC, f * pow(2.0, -5.0 / 12.0));
    double tri = band(L + SEC, 3 * SEC, f * pow(2.0, 6.0 / 12.0));
    CHECK(fifth > 100.0 * tri, "Fifth spray reaches fifths, not tritones");
    api->destroy_instance(h);
}

static void test_level(void) {
    printf("level\n");
    void *h = mk();
    plain_live(h);
    spf(h, "gr_size", 0.5f);
    spf(h, "gr_spray", 0.4f);
    spf(h, "gr_jit", 1.0f);
    render(h, 4 * SEC, NULL, NULL);
    float dens[] = {0.5f, 0.75f, 1.0f};
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
    /* every window has about Hann's loudness, and none clicks at its edges */
    spf(h, "gr_dens", 0.75f);
    float hann = 0;
    for (int w = 0; w < DRZ_WIN_COUNT; w++) {
        spf(h, "gr_win", (float)w);
        note_on(h, 48, 100);
        render(h, 4 * SEC, L, NULL);
        float r = rms(L + SEC, 3 * SEC);
        if (w == 0) hann = r;
        CHECK(fabsf(20.0f * log10f(r / hann)) < 4.0f, "window %s: %.2f dB from Hann", DRZ_PARAMS[P_GR_WIN].opts[w],
              (double)(20.0f * log10f(r / hann)));
        CHECK(all_finite(L, 4 * SEC), "window %d finite", w);
        note_off(h, 48);
        render(h, 4 * SEC, NULL, NULL);
    }
    CHECK(DRZ_WINDOWS[GR_WIN_HANN][0] == 0.0f && DRZ_WINDOWS[GR_WIN_RECT][0] == 0.0f &&
              DRZ_WINDOWS[GR_WIN_GAUSS][DRZ_WIN_N] < 1e-6f && DRZ_WINDOWS[GR_WIN_PERC][DRZ_WIN_N] < 1e-6f,
          "windows start and end at 0");
    api->destroy_instance(h);
}

/* Random positions, sizes, pitches, pitch spray, reverse, scan, buffer lengths and freezes: no grain
 * may read a sample that was not written at the position it means, or one written after "now". */
static void test_guard_fuzz(void) {
    printf("guard fuzz (Live)\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    plain_live(h);
    uint32_t rng = 777;
    for (int round = 0; round < 80; round++) {
        spf(h, "gr_pos", rng_uni(&rng));
        spf(h, "gr_spray", rng_uni(&rng));
        spf(h, "gr_size", rng_uni(&rng));
        spf(h, "gr_dens", rng_uni(&rng));
        spf(h, "gr_jit", rng_uni(&rng));
        spf(h, "gr_rev", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        spf(h, "gr_pspr", rng_uni(&rng) < 0.5f ? 0.0f : 24.0f * rng_uni(&rng));
        spf(h, "gr_pitch", (float)((int)(rng_uni(&rng) * 48) - 24));
        spf(h, "gr_scan", rng_uni(&rng) < 0.5f ? 0.0f : 8.0f * rng_uni(&rng) - 4.0f);
        spf(h, "gr_win", (float)(int)(rng_uni(&rng) * 6));
        spf(h, "buf_len", rng_uni(&rng));
        spf(h, "src_root", 24.0f + (float)(int)(72.0f * rng_uni(&rng)));
        spf(h, "gr_lock", rng_uni(&rng) < 0.5f ? 0.0f : rng_uni(&rng));
        sp(h, "src_track", rng_uni(&rng) < 0.3f ? "On" : "Off");
        spf(h, "me_pos", rng_uni(&rng) < 0.5f ? 0.0f : rng_bi(&rng));
        spf(h, "me_pitch", rng_uni(&rng) < 0.5f ? 0.0f : 24.0f * rng_bi(&rng));
        if (rng_uni(&rng) < 0.15f) sp(h, "buf_freeze", I->v[P_BUF_FREEZE] >= 0.5f ? "0" : "1");
        if (rng_uni(&rng) < 0.05f) sp(h, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"buf_mode\":\"Live\",\"rev_lvl\":\"0\"}");
        int note = 24 + (int)(72 * rng_uni(&rng));
        note_on(h, note, 100);
        render(h, (int)(SEC / 4 + SEC * rng_uni(&rng)) & ~127, L, NULL);
        if (rng_uni(&rng) < 0.6f) note_off(h, note);
    }
    CHECK(I->guard_violations == 0, "%u guard violations", I->guard_violations);
    CHECK(I->nan_resets == 0, "no NaN resets");
    printf("  %u onsets skipped (pool full, voice cap or too little buffer)\n", I->grains_skipped);
    api->destroy_instance(h);
}

static void test_freeze(void) {
    printf("freeze (Live)\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    plain_live(h);
    sp(h, "an_a_wave", "Sine");
    spf(h, "buf_len", 0.25f); /* 1 s */
    render(h, 2 * SEC, NULL, NULL);
    sp(h, "buf_freeze", "On");
    render(h, 1280, NULL, NULL);
    const buffer_t *b = &I->buf;
    CHECK(b->frozen && b->xf_k == DRZ_XFADE, "frozen, seam crossfade done");
    CHECK(b->lf == buffer_len_samples(I), "the loop is buf_len long (%d)", b->lf);
    const float *d = I->data[0];
    int32_t last = (b->ls + b->lf - 1) % DRZ_RING, first = b->ls;
    float step = 2.0f * DRZ_PI * semis_to_hz(48) / DRZ_FS;
    CHECK(fabsf(d[first] - d[last]) < 1.5f * step, "seam step %.4f (a sine step is %.4f)", (double)fabsf(d[first] - d[last]),
          (double)step);
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
    static char s[16384];
    sp(h, "buf_freeze", "On");
    render(h, 1280, NULL, NULL);
    api->get_param(h, "state", s, sizeof s);
    sp(h, "state", s);
    render(h, 128, NULL, NULL);
    CHECK(!b->frozen && b->wait_full, "after a load the freeze waits");
    render(h, SEC / 2, NULL, NULL);
    CHECK(!b->frozen, "still waiting at 0.5 s");
    render(h, SEC, NULL, NULL);
    CHECK(b->frozen && b->lf == buffer_len_samples(I), "frozen once 1 s is recorded");
    CHECK(I->guard_violations == 0, "guard: %u", I->guard_violations);
    api->destroy_instance(h);
}

/* Scan tapes (plan/05 section 4): rendered at 8x realtime, deterministic, swapped without a grain
 * ever reading a buffer under a render, held by Freeze. */
static void test_scan(void) {
    printf("scan tapes\n");
    void *h = mk(), *h2 = mk();
    drz_inst_t *I = drz_inst(h), *I2 = drz_inst(h2);
    const buffer_t *b = &I->buf;
    CHECK(b->mode == BUF_MODE_SCAN, "INIT is a Scan patch");
    int blocks = 0;
    while (b->tape_renders == 0 && blocks < 2000) {
        render(h, 128, NULL, NULL);
        blocks++;
    }
    int expect = (int)ceil(4.0 * 44100.0 / DRZ_TAPE_CHUNK) + 1;
    CHECK(b->tape_renders == 1 && b->front >= 0, "the first tape renders");
    CHECK(blocks <= expect + 1, "a 4 s tape takes %d blocks (8x realtime: %d)", blocks, expect);
    CHECK(b->tape_len[b->front] == buffer_len_samples(I), "the tape is buf_len long");
    while (I2->buf.tape_renders == 0) render(h2, 128, NULL, NULL);
    CHECK(memcmp(I->data[b->front], I2->data[I2->buf.front], sizeof(float) * (size_t)b->tape_len[b->front]) == 0,
          "two instances render the same tape bit for bit");
    /* Position is a timbre axis: a Sweep pitch of +12 puts the start an octave below the end. */
    sp(h2, "src_type", "Analog");
    sp(h2, "an_a_wave", "Sine");
    sp(h2, "sw_timb", "0");
    sp(h2, "sw_pitch", "12");
    int r0 = I2->buf.tape_renders;
    while (I2->buf.tape_renders == r0) render(h2, 128, NULL, NULL);
    const float *t = I2->data[I2->buf.front];
    int32_t tl = I2->buf.tape_len[I2->buf.front];
    float f0 = zc_hz(t, tl / 10), f1 = zc_hz(t + tl - tl / 10, tl / 10);
    CHECK(f1 > 1.8f * f0 && f1 < 2.05f * f0, "the tape sweeps an octave: %.0f Hz -> %.0f Hz", (double)f0, (double)f1);

    note_on(h, 48, 100);
    render(h, SEC, L, NULL);
    CHECK(rms(L + SEC / 2, SEC / 2) > 0.01f, "a note plays the tape");
    /* re-render while notes play: the render never writes a buffer a grain reads */
    int clash = 0, renders = b->tape_renders;
    for (int k = 0; k < 6; k++) {
        spf(h, "wt_a_wave", (float)(8 * (k + 1)));
        for (int i = 0; i < 300; i++) {
            render(h, 128, L, NULL);
            int back = b->front < 0 ? 0 : 1 - b->front;
            if (b->rendering && b->render_k > 0 && grains_reading(I, back)) clash++;
        }
    }
    CHECK(clash == 0, "no grain reads a buffer under a render (%d)", clash);
    CHECK(b->tape_renders >= renders + 6, "each change renders a tape (%d)", b->tape_renders - renders);
    /* a knob turned continuously never completes a render; it renders once it stops */
    renders = b->tape_renders;
    for (int i = 0; i < 400; i++) {
        spf(h, "src_fold", 0.001f * (float)(i + 1));
        render(h, 128, NULL, NULL);
    }
    CHECK(b->tape_renders == renders, "debounce: no render while the knob moves");
    for (int i = 0; i < 400; i++) render(h, 128, NULL, NULL);
    CHECK(b->tape_renders == renders + 1, "one render after it stops");
    /* Freeze holds the tape */
    sp(h, "buf_freeze", "On");
    renders = b->tape_renders;
    sp(h, "wt_a_table", "5");
    render(h, 2 * SEC, NULL, NULL);
    CHECK(b->tape_renders == renders, "Freeze holds the tape");
    sp(h, "buf_freeze", "Off");
    render(h, 2 * SEC, NULL, NULL);
    CHECK(b->tape_renders == renders + 1, "release renders the change");
    CHECK(I->guard_violations == 0, "guard: %u", I->guard_violations);
    /* buf_view */
    char v[512];
    int n = api->get_param(h, "buf_view", v, sizeof v);
    CHECK(n == 6 + 2 * DRZ_BINS && v[0] == 'S' && v[2] == 'F' && v[3] == 'F', "buf_view: %.12s… (%d)", v, n);
    int nonzero = 0;
    for (int i = 6; i < n; i += 2) nonzero += !(v[i] == '0' && v[i + 1] == '0');
    CHECK(nonzero > 100, "the tape's bins are drawn (%d)", nonzero);
    n = api->get_param(h, "grain_view", v, sizeof v);
    CHECK(n > 0 && n % 4 == 0, "grain_view (%d)", n);
    /* switching modes */
    sp(h, "buf_mode", "Live");
    render(h, SEC, L, NULL);
    CHECK(I->buf.mode == BUF_MODE_LIVE && rms(L + SEC / 2, SEC / 2) > 0.01f, "Scan -> Live keeps playing");
    sp(h, "buf_mode", "Scan");
    render(h, SEC, L, NULL);
    CHECK(I->buf.mode == BUF_MODE_SCAN && rms(L + SEC / 2, SEC / 2) > 0.01f, "Live -> Scan renders and plays");
    note_off(h, 48);
    api->destroy_instance(h);
    api->destroy_instance(h2);
}

static void test_scan_fuzz(void) {
    printf("guard fuzz (Scan)\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    uint32_t rng = 4242;
    for (int round = 0; round < 60; round++) {
        spf(h, "gr_pos", rng_uni(&rng));
        spf(h, "gr_spray", rng_uni(&rng));
        spf(h, "gr_size", rng_uni(&rng));
        spf(h, "gr_dens", rng_uni(&rng));
        spf(h, "gr_rev", rng_uni(&rng));
        spf(h, "gr_pitch", (float)((int)(rng_uni(&rng) * 48) - 24));
        spf(h, "gr_pspr", 24.0f * rng_uni(&rng));
        spf(h, "gr_scan", 8.0f * rng_uni(&rng) - 4.0f);
        if (rng_uni(&rng) < 0.3f) spf(h, "buf_len", rng_uni(&rng));
        if (rng_uni(&rng) < 0.3f) spf(h, "wt_a_wave", 63.0f * rng_uni(&rng));
        if (rng_uni(&rng) < 0.3f) spf(h, "gr_lock", rng_uni(&rng));
        if (rng_uni(&rng) < 0.2f) sp(h, "v_mode", rng_uni(&rng) < 0.5f ? "Poly" : "Legato");
        int note = 24 + (int)(72 * rng_uni(&rng));
        note_on(h, note, 100);
        render(h, (int)(SEC / 4 + SEC * rng_uni(&rng)) & ~127, L, NULL);
        if (!all_finite(L, SEC / 4)) I->nan_resets++;
        if (rng_uni(&rng) < 0.6f) note_off(h, note);
    }
    CHECK(I->guard_violations == 0, "%u tape reads past the ends", I->guard_violations);
    CHECK(I->nan_resets == 0, "finite");
    api->destroy_instance(h);
}

static void test_pool(void) {
    printf("pool\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    plain_live(h);
    spf(h, "gr_dens", 1.0f);
    spf(h, "gr_size", 1.0f);
    spf(h, "buf_len", 1.0f);
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
    int sum = 0;
    for (int v = 0; v < DRZ_VOICES; v++) sum += I->voice[v].n_grains;
    CHECK(sum == grains_active(I), "per-voice counts add up (%d vs %d)", sum, grains_active(I));
    api->destroy_instance(h);
}

static void test_fx(void) {
    printf("effects\n");
    void *h = mk();
    plain_live(h);
    render(h, SEC, NULL, NULL);
    sp(h, "rev_lvl", "0.6");
    sp(h, "dly_lvl", "0.5");
    sp(h, "cho_lvl", "0.5");
    sp(h, "cr_lvl", "0.4");
    for (int t = 0; t < 4; t++) {
        spf(h, "cr_type", (float)t);
        spf(h, "rev_type", (float)t);
        note_on(h, 48, 100);
        render(h, SEC, L, R);
        note_off(h, 48);
        render(h, 4 * SEC, L, R);
        CHECK(all_finite(L, 4 * SEC) && all_finite(R, 4 * SEC), "type %d finite", t);
        CHECK(rms(L + SEC, SEC / 2) > 1e-4f, "type %d: a tail after the release (%.5f)", t, (double)rms(L + SEC, SEC / 2));
    }
    api->destroy_instance(h);
}

/* ------------------------------------------------------------------ M2 */

static void test_wavetable(void) {
    printf("wavetable source\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    static float buf[SEC];
    int ok = 1;
    uint32_t rng = 5;
    for (int t = 0; t < 80; t++) {
        spf(h, "wt_a_table", (float)(int)(rng_uni(&rng) * DRZ_WT_TABLES));
        spf(h, "wt_b_table", (float)(int)(rng_uni(&rng) * DRZ_WT_TABLES));
        spf(h, "wt_a_wave", 63.0f * rng_uni(&rng));
        spf(h, "wt_b_lvl", rng_uni(&rng));
        spf(h, "wt_xmod", (float)(t % 4));
        spf(h, "wt_xamt", rng_uni(&rng));
        spf(h, "wt_q", (float)((t / 4) % 2));
        spf(h, "src_root", 24.0f + 72.0f * rng_uni(&rng));
        source_prepare(I);
        source_t s;
        source_reset(&s, 3);
        for (int k = 0; k < 4096; k += 16) source_render(&I->sp, &s, rng_uni(&rng), 0.0f, buf + k, 16);
        if (!all_finite(buf, 4096) || peak(buf, 4096) > 1.6f) ok = 0;
    }
    CHECK(ok, "finite and bounded over tables, waves, cross-mods and both qualities");
    for (int q = 0; q < 2; q++) {
        sp(h, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"sw_timb\":\"0\"}"); /* table 0, wave 0: a sine */
        spf(h, "wt_q", (float)q);
        source_prepare(I);
        source_t s;
        source_reset(&s, 3);
        for (int k = 0; k < SEC; k += 16) source_render(&I->sp, &s, 0.0f, 0.0f, buf + k, 16);
        float f = zc_hz(buf + 2048, SEC - 2048);
        CHECK(fabsf(f - semis_to_hz(48)) < 1.5f, "%s: wave 0 of table 0 plays the root (%.1f Hz)", q ? "Clean" : "Vintage",
              (double)f);
    }
    api->destroy_instance(h);
}

static void test_source_lfo(void) {
    printf("source LFO\n");
    void *h = mk(), *h2 = mk();
    drz_inst_t *I = drz_inst(h), *I2 = drz_inst(h2);
    const char *st = "{\"fmt\":\"drizzle\",\"v\":\"1\",\"src_type\":\"Analog\",\"an_a_wave\":\"Sine\",\"sw_timb\":\"0\","
                     "\"sl_pitch\":\"12\",\"sl_wave\":\"Square\",\"sl_rate\":\"0.388\",\"buf_len\":\"1\"}";
    sp(h, "state", st);
    sp(h2, "state", st);
    while (I->buf.tape_renders == 0) render(h, 128, NULL, NULL);
    while (I2->buf.tape_renders == 0) render(h2, 128, NULL, NULL);
    const float *t = I->data[I->buf.front];
    int32_t len = I->buf.tape_len[I->buf.front];
    CHECK(memcmp(t, I2->data[I2->buf.front], sizeof(float) * (size_t)len) == 0, "an LFO tape is deterministic");
    /* sl_rate 0.388 = 0.25 Hz: on an 8 s tape, 2 s an octave up, then 2 s an octave down */
    float hz = 0.01f * powf(4000.0f, 0.388f);
    int half = (int)(DRZ_FS * 0.5f / hz);
    if (half > len / 2) half = len / 2;
    float f0 = zc_hz(t + 1000, half / 2), f1 = zc_hz(t + half + 1000, half / 2);
    CHECK(f0 > 1.8f * f1 || f1 > 1.8f * f0, "a square Source LFO switches the tape an octave (%.0f / %.0f Hz)", (double)f0,
          (double)f1);
    api->destroy_instance(h);
    api->destroy_instance(h2);
}

/* Period lock: grains of one voice line up, so a sine cloud stays a clean sine at the note. */
static void test_lock(void) {
    printf("period lock\n");
    void *h = mk();
    plain_live(h);
    sp(h, "an_a_wave", "Sine");
    spf(h, "gr_spray", 0.3f);
    spf(h, "gr_size", 0.6f);
    spf(h, "gr_dens", 0.75f);
    render(h, 4 * SEC, NULL, NULL);
    double clean[2], lvl[2];
    for (int k = 0; k < 2; k++) {
        spf(h, "gr_lock", (float)k);
        note_on(h, 55, 100);
        render(h, 4 * SEC, L, NULL);
        double f = (double)semis_to_hz(55);
        double on = band(L + SEC, 3 * SEC, f), off = 0;
        for (int j = 1; j <= 8; j++) off += band(L + SEC, 3 * SEC, f * (1.0 + 0.06 * j));
        clean[k] = on / (off + 1e-9);
        lvl[k] = (double)rms(L + SEC, 3 * SEC);
        note_off(h, 55);
        render(h, 4 * SEC, NULL, NULL);
    }
    /* What remains with lock 1 is the windows' amplitude modulation (sidebands at the grain rate). */
    CHECK(clean[1] > 5.0 * clean[0], "lock 1 is at least 7 dB cleaner around the note (%.0f vs %.0f)", clean[1], clean[0]);
    CHECK(fabs(20.0 * log10(lvl[1] / lvl[0])) < 4.0, "lock keeps the level (%.1f dB)", 20.0 * log10(lvl[1] / lvl[0]));
    api->destroy_instance(h);
}

static void test_filter(void) {
    printf("filter\n");
    void *h = mk();
    plain_live(h);
    spf(h, "gr_dens", 0.75f);
    render(h, 2 * SEC, NULL, NULL);
    float base;
    note_on(h, 48, 100);
    render(h, 2 * SEC, L, NULL);
    base = rms(L + SEC, SEC);
    sp(h, "flt_type", "LP24");
    spf(h, "flt_cut", 0.2f);
    spf(h, "flt_key", 0.0f);
    render(h, 2 * SEC, L, NULL);
    float lp = rms(L + SEC, SEC);
    CHECK(lp < 0.5f * base, "LP24 at a low cutoff takes the saw's harmonics (%.3f vs %.3f)", (double)lp, (double)base);
    sp(h, "flt_type", "HP12");
    spf(h, "flt_cut", 0.95f);
    render(h, 2 * SEC, L, NULL);
    CHECK(rms(L + SEC, SEC) < 0.5f * base, "HP12 near the top thins it");
    sp(h, "flt_type", "BP12");
    spf(h, "flt_res", 1.0f);
    spf(h, "flt_drive", 1.0f);
    spf(h, "flt_env", 1.0f);
    render(h, 3 * SEC, L, R);
    CHECK(all_finite(L, 3 * SEC) && all_finite(R, 3 * SEC) && peak(L, 3 * SEC) < 1.0f, "resonant, driven and swept: bounded");
    api->destroy_instance(h);
}

static void test_voice_modes(void) {
    printf("voice modes, glide, flavour, tracking\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    plain_live(h);
    render(h, SEC, NULL, NULL);
    sp(h, "v_mode", "Mono");
    note_on(h, 48, 100);
    note_on(h, 52, 100);
    render(h, 1280, NULL, NULL);
    int act = 0;
    for (int v = 0; v < DRZ_VOICES; v++) act += I->voice[v].active && I->voice[v].gate;
    CHECK(act == 1 && I->voice[0].note == 52, "Mono plays the newest key on one voice");
    note_off(h, 52);
    render(h, 128, NULL, NULL);
    CHECK(I->voice[0].gate && I->voice[0].note == 48, "releasing it returns to the held key");
    note_off(h, 48);
    render(h, 4 * SEC, NULL, NULL);
    sp(h, "v_mode", "Legato");
    sp(h, "v_glide", "0.7");
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    int stage = I->voice[0].amp.stage;
    note_on(h, 60, 100);
    render(h, 128, NULL, NULL);
    CHECK(I->voice[0].amp.stage == stage && stage == ES_SUSTAIN, "Legato keeps the envelope running");
    float p1 = I->voice[0].pitch;
    CHECK(p1 > 48.5f && p1 < 59.5f, "the pitch glides (%.2f after 3 ms)", (double)p1);
    render(h, 2 * SEC, NULL, NULL);
    CHECK(fabsf(I->voice[0].pitch - 60.0f) < 0.05f, "and arrives");
    note_off(h, 48);
    note_off(h, 60);
    render(h, 4 * SEC, NULL, NULL);
    sp(h, "v_mode", "Poly");
    sp(h, "v_glide", "0");
    sp(h, "v_flav", "1");
    note_on(h, 50, 100);
    float f1 = I->voice[0].flav[0];
    note_off(h, 50);
    render(h, 4 * SEC, NULL, NULL);
    note_on(h, 50, 100);
    CHECK(fabsf(I->voice[0].flav[0] - f1) > 1e-6f && fabsf(f1) <= 0.1f, "Flavour differs per note, within 0.1");
    note_off(h, 50);
    render(h, 4 * SEC, NULL, NULL);
    /* Track Notes: the newest key's grains read the source at about its own pitch */
    sp(h, "v_flav", "0");
    sp(h, "src_track", "On");
    spf(h, "gr_spray", 0.0f);
    spf(h, "gr_pos", 0.02f);
    note_on(h, 48, 100);
    render(h, SEC / 2, NULL, NULL);
    note_on(h, 55, 100);
    render(h, SEC / 2, NULL, NULL);
    int vi = I->voice[0].note == 55 ? 0 : 1, near = 0, total = 0;
    for (int b = 0; b < 344; b++) { /* every grain the newest key starts over a second */
        render(h, 128, NULL, NULL);
        for (int k = 0; k < I->n_live; k++) {
            const grain_t *g = &I->grain[I->live[k]];
            if (g->voice != vi || g->age > 128) continue;
            total++;
            double inc = (double)g->dpos / 4294967296.0;
            near += fabs(inc - 1.0) < 0.01;
        }
    }
    CHECK(total > 0 && near == total, "new grains of the newest key read at step 1 (%d of %d)", near, total);
    /* Scale quantise */
    I->scale_mask = 0x0AB5; /* C major */
    I->scale_root = 0;
    CHECK(scale_quantise(I, 61.0f) == 60.0f && scale_quantise(I, 66.0f) == 65.0f && scale_quantise(I, 62.2f) == 62.0f,
          "Scale snaps to C major");
    CHECK(I->guard_violations == 0, "guard: %u", I->guard_violations);
    api->destroy_instance(h);
}

/* ------------------------------------------------------------------ M3 */

static void test_tempo(void) {
    printf("tempo, probability, repeats, random\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    sp(h, "state", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"gr_mode\":\"Tempo\",\"gr_div\":\"1/16\",\"rev_lvl\":\"0\",\"gr_size\":\"0.2\"}");
    render(h, SEC, NULL, NULL); /* the tape */
    /* internal clock: the first grain at note-on, then every 1/16 at 120 BPM = 5512.5 samples */
    I->n_onsets = 0;
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    int ok = I->n_onsets >= 7, worst = 0;
    for (int i = 1; i < I->n_onsets && i < 8; i++) {
        double want = (double)I->onset_t[0] + 5512.5 * i;
        int err = (int)fabs((double)I->onset_t[i] - want);
        if (err > worst) worst = err;
    }
    CHECK(ok && worst <= 1, "1/16 at 120 BPM: %d onsets, worst error %d samples", I->n_onsets, worst);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    /* host transport: steps sit on the host's beat grid */
    g_run = 1;
    g_beat = 0.3; /* mid-step */
    I->n_onsets = 0;
    render(h, 1280, NULL, NULL);
    note_on(h, 48, 100);
    int64_t t0 = I->clock;
    double b0 = g_beat;
    render(h, SEC, NULL, NULL);
    worst = 0;
    for (int i = 0; i < I->n_onsets && i < 8; i++) {
        double beat = b0 + (double)(I->onset_t[i] - t0) * 120.0 / (60.0 * 44100.0);
        double off = (beat * 4.0 - floor(beat * 4.0 + 0.5)) / 4.0 * 22050.0; /* samples from the nearest step */
        if ((int)fabs(off) > worst) worst = (int)fabs(off);
    }
    CHECK(I->n_onsets >= 7 && worst <= 128, "steps on the host's grid (worst %d samples, block-accurate beat)", worst);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    /* swing: every second step is late by swing * step / 2 */
    sp(h, "gr_swing", "0.5");
    g_beat = 0.0;
    I->n_onsets = 0;
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    int64_t d1 = I->onset_t[2] - I->onset_t[1], d2 = I->onset_t[3] - I->onset_t[2];
    CHECK(I->n_onsets >= 4 && llabs(llabs(d1 - d2) - (int64_t)(5512.5 * 0.5)) < 140, "swing 0.5 (%lld / %lld)", (long long)d1,
          (long long)d2);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    sp(h, "gr_swing", "0");
    /* probability */
    sp(h, "gr_prob", "0");
    I->n_onsets = 0;
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    CHECK(I->n_onsets == 0, "probability 0: no grains (%d)", I->n_onsets);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    sp(h, "gr_prob", "0.5");
    I->n_onsets = 0;
    note_on(h, 48, 100);
    render(h, 4 * SEC, NULL, NULL);
    CHECK(I->n_onsets > 10 && I->n_onsets < 22, "probability 0.5: about half of 32 steps (%d)", I->n_onsets);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    sp(h, "gr_prob", "1");
    /* repeats: each fresh position is played 1 + 3 times */
    sp(h, "gr_rpt", "3");
    sp(h, "gr_spray", "1");
    I->n_onsets = 0;
    note_on(h, 48, 100);
    render(h, 2 * SEC, NULL, NULL);
    int groups = 1;
    for (int i = 1; i < 16 && i < I->n_onsets; i++)
        if (i % 4 == 0) groups += I->onset_q[i] != I->onset_q[i - 1];
        else if (I->onset_q[i] != I->onset_q[i - 1]) groups = -100;
    CHECK(groups == 4, "repeats 3: four grains per position (%d)", groups);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    sp(h, "gr_rpt", "0");
    /* Random = Bar: with the transport running, two bars render bit for bit the same */
    sp(h, "gr_rnd", "Bar");
    sp(h, "gr_pspr", "12");
    g_beat = 0.0;
    note_on(h, 48, 100);
    render(h, 88192 / 128 * 128, NULL, NULL); /* settle into the first bar (the envelope's attack) */
    while (fmod(g_beat, 4.0) > 1e-9 && fmod(g_beat, 4.0) < 4.0 - 1e-9) render(h, 128, NULL, NULL);
    static float A[88200], B[88200];
    render(h, 88192, A, NULL);
    while (fmod(g_beat, 4.0) > 1e-9 && fmod(g_beat, 4.0) < 4.0 - 1e-9) render(h, 128, NULL, NULL);
    render(h, 88192, B, NULL);
    float diff = 0;
    for (int i = 0; i < 88192; i++) diff = fmaxf(diff, fabsf(A[i] - B[i]));
    CHECK(rms(A, 88192) > 0.005f && diff < 1e-4f, "two bars alike (max difference %.6f)", (double)diff);
    note_off(h, 48);
    g_run = 0;
    api->destroy_instance(h);
}

static void test_modulation(void) {
    printf("LFOs, pressure, matrix, macros\n");
    void *h = mk();
    drz_inst_t *I = drz_inst(h);
    render(h, SEC, NULL, NULL);
    /* the matrix through its proxies and list page */
    sp(h, "mm_sel", "2");
    sp(h, "mm_src", "Mod Wheel");
    sp(h, "mm_dst", "Position");
    sp(h, "mm_dep", "0.4");
    CHECK(I->v[P_MM03_SRC] == MS_MOD_WHEEL && I->v[P_MM03_DST] == MD_POSITION && fabsf(I->v[P_MM03_DEP] - 0.4f) < 1e-6f,
          "proxies write slot 3");
    char items[2048];
    api->get_param(h, "mm_items", items, sizeof items);
    CHECK(strstr(items, "\"3: Whl>Pos\"") != NULL, "mm_items: %.80s", items);
    sp(h, "mm_sel", "0");
    CHECK(gp(h, "mm_src") == 0.0f && gp(h, "mm_slot") == 1.0f, "selecting slot 1 shows it");
    sp(h, "mm_slot", "3");
    CHECK(gp(h, "mm_src") == (float)MS_MOD_WHEEL, "mm_slot selects too");
    note_on(h, 48, 100);
    midi(h, 0xB0, 1, 127);
    render(h, 1280, NULL, NULL);
    CHECK(fabsf(gp(h, "gr_pos:effective") - 0.9f) < 0.01f, "wheel x 0.4 -> Position: effective %.3f", (double)gp(h, "gr_pos:effective"));
    sp(h, "mm_via", "Macro 1");
    render(h, 1280, NULL, NULL);
    CHECK(fabsf(gp(h, "gr_pos:effective") - 0.5f) < 0.01f, "via Macro 1 at 0 cancels it");
    sp(h, "mac1", "0.5");
    render(h, 2560, NULL, NULL);
    CHECK(fabsf(gp(h, "gr_pos:effective") - 0.7f) < 0.02f, "Macro 1 at 0.5 halves it (%.3f)", (double)gp(h, "gr_pos:effective"));
    sp(h, "mm_clear", "Clear");
    CHECK(I->v[P_MM03_SRC] == 0.0f && I->v[P_MM03_DST] == 0.0f, "Clear empties the slot");
    /* pressure: poly aftertouch raises the density through at_dens (0.3 in INIT) */
    float d0 = gp(h, "gr_dens:effective");
    midi(h, 0xA0, 48, 127);
    render(h, 2560, NULL, NULL);
    CHECK(gp(h, "gr_dens:effective") > d0 + 0.05f, "poly AT -> density (%.3f -> %.3f)", (double)d0,
          (double)gp(h, "gr_dens:effective"));
    /* LFO 1 moves Position */
    sp(h, "l1_pos", "0.5");
    sp(h, "l1_rate", "0.6");
    float lo = 9, hi = -9;
    for (int b = 0; b < 344; b++) {
        render(h, 128, NULL, NULL);
        float e = gp(h, "gr_pos:effective");
        lo = fminf(lo, e);
        hi = fmaxf(hi, e);
    }
    CHECK(hi - lo > 0.8f, "LFO 1 sweeps Position (%.2f..%.2f)", (double)lo, (double)hi);
    note_off(h, 48);
    render(h, 3 * SEC, NULL, NULL);
    /* LFO 2 synced to the bar follows the host's beat */
    sp(h, "l2_sync", "1 bar");
    sp(h, "l2_wave", "Sine");
    g_run = 1;
    g_beat = 1.0; /* a quarter into the bar: sine at its peak */
    render(h, 128, NULL, NULL);
    CHECK(I->l2_val > 0.99f, "LFO 2 at beat 1 of a 1-bar sine: %.3f", (double)I->l2_val);
    g_run = 0;
    api->destroy_instance(h);
}

static void test_reverb_modes(void) {
    printf("reverb cloud and freeze\n");
    void *h = mk();
    plain_live(h);
    sp(h, "rev_lvl", "0.8");
    sp(h, "rev_type", "Cloud");
    sp(h, "rev_decay", "1");
    render(h, SEC, NULL, NULL);
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    note_off(h, 48);
    render(h, 6 * SEC, L, R);
    CHECK(all_finite(L, 6 * SEC) && rms(L + 5 * SEC, SEC) > 0.005f, "Cloud at full decay still rings 6 s later");
    sp(h, "rev_type", "Hall");
    sp(h, "rev_decay", "0.3");
    note_on(h, 48, 100);
    render(h, SEC, NULL, NULL);
    sp(h, "rev_frz", "On");
    note_off(h, 48);
    render(h, 4 * SEC, NULL, NULL);
    render(h, SEC, L, NULL);
    float a = rms(L, SEC);
    render(h, 20 * SEC, NULL, NULL);
    render(h, SEC, L, NULL);
    float b = rms(L, SEC);
    CHECK(a > 0.005f && fabsf(20.0f * log10f(b / a)) < 1.5f, "a frozen reverb holds its level for 20 s (%.2f dB)",
          (double)(20.0f * log10f(b / a)));
    sp(h, "rev_frz", "Off");
    render(h, 8 * SEC, NULL, NULL);
    render(h, SEC, L, NULL);
    CHECK(rms(L, SEC) < 0.25f * b, "released, it decays");
    api->destroy_instance(h);
}

int main(void) {
    memset(&host, 0, sizeof host);
    host.get_bpm = fake_bpm;
    host.get_clock_status = fake_clock;
    host.get_beat_position = fake_beat;
    api = move_plugin_init_v2(&host);
    test_abi();
    test_params();
    test_state();
    test_presets();
    test_source();
    test_render();
    test_pitch();
    test_level();
    test_guard_fuzz();
    test_freeze();
    test_scan();
    test_scan_fuzz();
    test_pool();
    test_fx();
    test_wavetable();
    test_source_lfo();
    test_lock();
    test_filter();
    test_voice_modes();
    test_tempo();
    test_modulation();
    test_reverb_modes();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
