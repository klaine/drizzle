/* Render one demo WAV (16-bit stereo, 44.1 kHz) per factory patch for listening:
 * build/drizzle_render <outdir>. A C minor chord builds up over two seconds, holds, and is released. */
#define _POSIX_C_SOURCE 200809L /* nanosleep: sample patches wait for the loader */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    void *h = api->create_instance(".", "");
    char buf[64];
    api->get_param(h, "preset_count", buf, sizeof buf);
    int n = atoi(buf);
    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof buf, "%d", i);
        sp(h, "preset", buf);
        for (int b = 0; b < S(2); b++) api->render_block(h, out, 128); /* the tape or ring fills */
        for (int t = 0; t < 1000; t++) { /* a sample patch: its file loads */
            char st[64];
            api->get_param(h, "smp_status", st, sizeof st);
            if (strcmp(st, "loading") != 0) break;
            struct timespec ts = {0, 5 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        api->render_block(h, out, 128);
        char name[64], file[80];
        api->get_param(h, "preset_name", name, sizeof name);
        int k = 0;
        for (const char *c = name; *c && k < 60; c++) file[k++] = (*c == ' ') ? '_' : (char)tolower((unsigned char)*c);
        file[k] = 0;
        char fname[96];
        snprintf(fname, sizeof fname, "%02d_%s", i, file);
        ev_t ev[] = {{0, 0x90, 48, 100}, {S(0.7), 0x90, 51, 90}, {S(1.4), 0x90, 55, 90}, {S(6), 0x80, 48, 0},
                     {S(6), 0x80, 51, 0}, {S(6), 0x80, 55, 0}};
        render(dir, fname, h, ev, 6, S(10));
        midi(h, 0xB0, 120, 0);
    }
    api->destroy_instance(h);
    return 0;
}
