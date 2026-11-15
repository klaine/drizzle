/* Golden audio (plan/13 section 3): render every factory patch and a few feature cases, and print a
 * fingerprint of each as JSON for tests/golden/check_golden.py. Fixed seeds make every render
 * deterministic; the fingerprint (per 250 ms: RMS of each channel in dB, the zero-crossing rate and the
 * peak in dB)
 * is robust to the last-bit float differences between machines.
 *
 *   build/drizzle_golden > fingerprints.json */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drizzle.h"

static plugin_api_v2_t *api;
static host_api_v1_t host;
static float fake_bpm(void) { return 120.0f; }

#define SLICE 11025 /* 250 ms */
#define SLICES 16   /* 4 s: a 3 s chord and 1 s of release */

static void midi(void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

/* Fill the buffer (1.5 s, so a Scan tape is ready), play a C minor chord for 3 s, release, 1 s. */
static void case_render(const char *name, void *h, int first) {
    int16_t blk[256];
    for (int b = 0; b < 517; b++) api->render_block(h, blk, 128);
    static const int CHORD[3] = {48, 51, 55};
    for (int k = 0; k < 3; k++) midi(h, 0x90, CHORD[k], 100);
    double sl[2] = {0, 0}, pk = 0;
    long zc = 0, n = 0, total = 0;
    float prev = 0.0f;
    printf("%s  \"%s\": [", first ? "" : ",\n", name);
    int slice = 0;
    while (slice < SLICES) {
        if (total == 3L * 44100) for (int k = 0; k < 3; k++) midi(h, 0x80, CHORD[k], 0);
        api->render_block(h, blk, 128);
        for (int i = 0; i < 128; i++) {
            float l = blk[2 * i] / 32768.0f, r = blk[2 * i + 1] / 32768.0f;
            sl[0] += (double)l * (double)l;
            sl[1] += (double)r * (double)r;
            zc += (prev < 0.0f) != (l < 0.0f);
            if (fabs((double)l) > pk) pk = fabs((double)l);
            if (fabs((double)r) > pk) pk = fabs((double)r);
            prev = l;
            if (++n == SLICE) {
                double dl = 10.0 * log10(sl[0] / SLICE + 1e-12), dr = 10.0 * log10(sl[1] / SLICE + 1e-12);
                printf("%s[%.2f,%.2f,%.4f,%.2f]", slice ? "," : "", dl, dr, (double)zc / SLICE, 20.0 * log10(pk + 1e-9));
                sl[0] = sl[1] = pk = 0;
                zc = n = 0;
                slice++;
            }
        }
        total += 128;
    }
    printf("]");
    midi(h, 0xB0, 120, 0);
}

int main(void) {
    memset(&host, 0, sizeof host);
    host.get_bpm = fake_bpm;
    api = move_plugin_init_v2(&host);
    void *h = api->create_instance(".", "");
    char buf[64];
    api->get_param(h, "preset_count", buf, sizeof buf);
    int count = atoi(buf);
    printf("{\n");
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof buf, "%d", i);
        api->set_param(h, "preset", buf);
        char name[64], key[80];
        api->get_param(h, "preset_name", name, sizeof name);
        snprintf(key, sizeof key, "patch %02d %s", i, name);
        case_render(key, h, i == 0);
    }
    /* feature cases that the factory bank may not cover */
    static const char *const CASES[][2] = {
        {"live freeze", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"buf_mode\":\"Live\",\"buf_freeze\":\"On\",\"buf_len\":\"0.25\"}"},
        {"locked cloud", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"gr_lock\":\"1\",\"gr_spray\":\"0.3\",\"gr_dens\":\"0.75\"}"},
        {"filter mono glide", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"v_mode\":\"Legato\",\"v_glide\":\"0.6\",\"flt_type\":\"LP24\","
                              "\"flt_cut\":\"0.4\",\"flt_res\":\"0.6\",\"flt_env\":\"0.5\",\"me_a\":\"0.5\"}"},
        {"clean wavetable lfo", "{\"fmt\":\"drizzle\",\"v\":\"1\",\"wt_q\":\"Clean\",\"wt_a_table\":\"20\",\"sl_timb\":\"0.5\","
                                "\"sl_rate\":\"0.4\",\"gr_rev\":\"0.5\",\"gr_pspr\":\"12\"}"},
    };
    for (size_t c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
        api->set_param(h, "state", CASES[c][1]);
        case_render(CASES[c][0], h, 0);
    }
    printf("\n}\n");
    return 0;
}
