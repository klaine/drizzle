#define _POSIX_C_SOURCE 200809L /* clock_gettime */
/* Native CPU benchmark: time render_block for a few patches (plan/13 section 5).
 * The build machine is several times faster than Move's Cortex-A72: read the numbers as a ratio
 * against the 2902 us block, then calibrate on the device with the cpu_us key (--profile). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "drizzle.h"

static plugin_api_v2_t *api;

static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }
static void note_on(void *h, int n) {
    uint8_t m[3] = {0x90, (uint8_t)n, 100};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec * 1e-3;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y);
}

#define N 3000

static void run(const char *name, void *h, int voices, int base) {
    static double t[N];
    int16_t out[256];
    for (int i = 0; i < 700; i++) api->render_block(h, out, 128); /* fill the ring */
    for (int i = 0; i < voices; i++) note_on(h, base + 5 * i);
    for (int i = 0; i < 400; i++) api->render_block(h, out, 128); /* the clouds build up */
    for (int i = 0; i < N; i++) {
        double a = now_us();
        api->render_block(h, out, 128);
        t[i] = now_us() - a;
    }
    qsort(t, N, sizeof t[0], cmp);
    double sum = 0;
    for (int i = 0; i < N; i++) sum += t[i];
    drz_inst_t *I = drz_inst(h);
    printf("%-38s mean %6.1f us   p99 %6.1f us   max %6.1f us   (%4.1f%% of a block at p99)  grains %3d\n", name,
           sum / N, t[N * 99 / 100], t[N - 1], t[N * 99 / 100] / 2902.0 * 100.0, grains_active(I));
    uint8_t m[3] = {0xB0, 120, 0};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    for (int i = 0; i < 40; i++) api->render_block(h, out, 128);
}

int main(void) {
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    api = move_plugin_init_v2(&host);
    void *h = api->create_instance(".", "");
    run("idle", h, 0, 48);
    run("INIT, 1 note", h, 1, 48);
    run("INIT, 8 notes", h, 8, 40);
    sp(h, "gr_dens", "0.85");
    sp(h, "gr_size", "0.75");
    run("8 notes, dense (113/s, 450 ms)", h, 8, 40);
    sp(h, "gr_dens", "1");
    sp(h, "gr_size", "1");
    sp(h, "buf_len", "1");
    run("8 notes, pool full (128 grains)", h, 8, 40);
    run("8 notes, pool full, pitched up", h, 8, 60);
    /* the worst case of plan/03 section 7: full pool, stereo LP24 on every voice, every effect, a
     * period-locked cloud, and a Scan tape re-rendering all the time */
    sp(h, "flt_type", "LP24");
    sp(h, "flt_res", "0.6");
    sp(h, "flt_drive", "0.5");
    sp(h, "cr_lvl", "0.3");
    sp(h, "cho_lvl", "0.5");
    sp(h, "dly_lvl", "0.5");
    sp(h, "rev_lvl", "0.5");
    sp(h, "gr_lock", "1");
    run("worst: pool full, LP24, all FX, lock", h, 8, 40);
    sp(h, "buf_mode", "Scan");
    sp(h, "src_type", "Wavetable");
    static int16_t out[256];
    for (int i = 0; i < 2000; i++) api->render_block(h, out, 128);
    note_on(h, 40); note_on(h, 45); note_on(h, 50); note_on(h, 55);
    note_on(h, 60); note_on(h, 65); note_on(h, 70); note_on(h, 75);
    {   /* re-render the tape continuously: change a source knob, then time the render blocks */
        static double t[N];
        int k = 0;
        for (int i = 0; i < 40 && k < N; i++) {
            char w[16];
            snprintf(w, sizeof w, "%d", i % 64);
            sp(h, "wt_a_wave", w);
            for (int b = 0; b < 300 && k < N; b++) {
                double a = now_us();
                api->render_block(h, out, 128);
                t[k++] = now_us() - a;
            }
        }
        qsort(t, (size_t)k, sizeof t[0], cmp);
        printf("%-38s p99 %6.1f us   max %6.1f us\n", "worst + tape re-render (Scan)", t[k * 99 / 100], t[k - 1]);
    }
    return 0;
}
