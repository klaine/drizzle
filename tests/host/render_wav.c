/* Render a few demo WAVs (16-bit stereo, 44.1 kHz) for listening: build/drizzle_render <outdir>.
 * M0 has no factory bank yet, so these are hand-set patches of the M0 parameters. */
#include <stdio.h>
#include <string.h>

#include "drizzle.h"

static plugin_api_v2_t *api;

static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }
static void midi(void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

static void put32(FILE *f, unsigned v) {
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, f);
}
static void put16(FILE *f, unsigned v) {
    unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, f);
}

typedef struct {
    int at_block, status, d1, d2;
} ev_t;

/* Render `blocks` blocks, applying events at their block, into <dir>/<name>.wav. */
static void render(const char *dir, const char *name, void *h, const ev_t *ev, int nev, int blocks) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    unsigned bytes = (unsigned)blocks * 128u * 4u;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, 44100);
    put32(f, 44100 * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    int16_t out[256];
    for (int b = 0; b < blocks; b++) {
        for (int e = 0; e < nev; e++)
            if (ev[e].at_block == b) midi(h, ev[e].status, ev[e].d1, ev[e].d2);
        api->render_block(h, out, 128);
        for (int i = 0; i < 256; i++) put16(f, (unsigned)(uint16_t)out[i]);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

#define S(sec) ((int)((sec) * 344.5))

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    api = move_plugin_init_v2(&host);
    int16_t out[256];

    /* INIT: a C minor chord on the saw cloud, then the release. */
    void *h = api->create_instance(".", "");
    for (int i = 0; i < S(4); i++) api->render_block(h, out, 128);
    ev_t chord[] = {{0, 0x90, 48, 100}, {S(0.4), 0x90, 51, 100}, {S(0.8), 0x90, 55, 100}, {S(6), 0x80, 48, 0},
                    {S(6), 0x80, 51, 0}, {S(6), 0x80, 55, 0}};
    render(dir, "m0_init_chord", h, chord, 6, S(8));
    api->destroy_instance(h);

    /* A wide, sprayed cloud: long grains, full jitter, a sine source. */
    h = api->create_instance(".", "");
    sp(h, "an_a_wave", "Sine");
    sp(h, "gr_spray", "0.8");
    sp(h, "gr_size", "0.85");
    sp(h, "gr_dens", "0.6");
    sp(h, "gr_jit", "1");
    sp(h, "gr_spread", "1");
    for (int i = 0; i < S(4); i++) api->render_block(h, out, 128);
    ev_t pad[] = {{0, 0x90, 48, 100}, {0, 0x90, 55, 100}, {0, 0x90, 62, 100}, {S(7), 0x80, 48, 0},
                  {S(7), 0x80, 55, 0}, {S(7), 0x80, 62, 0}};
    render(dir, "m0_sine_cloud", h, pad, 6, S(9));
    api->destroy_instance(h);

    /* Jitter 0 at a high density: the grain rate is heard as a buzz of its own. */
    h = api->create_instance(".", "");
    sp(h, "gr_jit", "0");
    sp(h, "gr_dens", "0.8");
    sp(h, "gr_size", "0.3");
    for (int i = 0; i < S(4); i++) api->render_block(h, out, 128);
    ev_t buzz[] = {{0, 0x90, 48, 100}, {S(3), 0x80, 48, 0}, {S(3.2), 0x90, 43, 100}, {S(6), 0x80, 43, 0}};
    render(dir, "m0_pulse_train", h, buzz, 4, S(7.5));
    api->destroy_instance(h);

    /* Freeze with CC 69, then a high chord over the frozen 1 s loop. */
    h = api->create_instance(".", "");
    sp(h, "buf_len", "0.25");
    sp(h, "gr_spray", "0.6");
    for (int i = 0; i < S(2); i++) api->render_block(h, out, 128);
    ev_t frz[] = {{0, 0xB0, 69, 127}, {1, 0xB0, 69, 0}, {S(0.1), 0x90, 60, 90}, {S(0.1), 0x90, 67, 90},
                  {S(5), 0x80, 60, 0}, {S(5), 0x80, 67, 0}};
    render(dir, "m0_frozen", h, frz, 6, S(7));
    api->destroy_instance(h);
    return 0;
}
