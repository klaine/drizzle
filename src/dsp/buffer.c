/* The source buffer (plan/05): the Live ring with Freeze, and Scan tapes.
 *
 * Live: the ring always wraps at DRZ_RING (8 s); buf_len only limits how far back grains read, so
 * changing it never scrambles what is recorded. Nothing clears the ring: `valid` counts the samples
 * written since the last reset, and grains never read further back than that.
 *
 * Scan: the source renders buf_len seconds into the back buffer, DRZ_TAPE_CHUNK samples per block,
 * with the Sweep spanning the tape. When it is done the back buffer becomes the front; grains already
 * playing keep the buffer they started in, and the next render waits until none reads it. */
#include <math.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

static void bins_clear(uint8_t *bins) { memset(bins, 0, DRZ_VIEW); }

/* Start over with nothing readable. The head keeps its absolute position, so grains still fading out
 * (a state load) keep reading what they meant to: new writes land ahead of them. */
void buffer_reset(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    int64_t w = b->w;
    int renders = b->tape_renders;
    memset(b, 0, sizeof *b);
    b->w = w;
    b->tape_renders = renders;
    b->xf_k = DRZ_XFADE;
    b->mode = (int)I->v[P_BUF_MODE];
    b->front = -1;
    b->debounce = b->mode == BUF_MODE_SCAN ? 1 : 0; /* a tape renders straight away */
    source_reset(&I->src_live, (uint32_t)I->v[P_SRC_SEED]);
}

int32_t buffer_len_samples(const drz_inst_t *I) {
    int32_t n = (int32_t)(map_buf_len_s(I->v[P_BUF_LEN]) * DRZ_FS);
    return n < DRZ_RING ? n : DRZ_RING;
}

int32_t buffer_readable(const drz_inst_t *I) {
    const buffer_t *b = &I->buf;
    if (I->sp.type == SRC_TYPE_SAMPLE) return I->smp_ready ? I->smp_frames : 0;
    if (b->mode == BUF_MODE_SCAN) return b->front >= 0 ? b->tape_len[b->front] : 0;
    if (b->frozen) return b->lf;
    int32_t n = buffer_len_samples(I);
    return n < b->valid ? n : b->valid;
}

void buffer_set_mode(drz_inst_t *I, int mode) {
    grains_kill_all(I);
    I->v[P_BUF_MODE] = (float)mode;
    int frz = I->v[P_BUF_FREEZE] >= 0.5f;
    buffer_reset(I);
    if (frz && mode == BUF_MODE_LIVE) I->buf.wait_full = 1;
}

/* A source key changed. Live hears it at once; Scan renders a new tape 60 ms after the last change,
 * dropping a render under way. While Freeze is on in Scan, the change waits for its release. */
void buffer_source_changed(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    if (b->mode != BUF_MODE_SCAN) return;
    b->rendering = 0;
    b->debounce = DRZ_TAPE_DEBOUNCE;
}

/* ------------------------------------------------------------------ Live and Freeze */

/* Freeze the newest min(buf_len, valid) samples as a loop; the source runs on for DRZ_XFADE samples,
 * crossfaded (equal power) into the start of the loop, so its end runs into its start without a seam. */
static void start_freeze(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    int32_t lf = buffer_len_samples(I);
    if (lf > b->valid) lf = b->valid;
    b->lf = lf;
    b->ls = (int32_t)((b->w - lf) % DRZ_RING);
    b->frozen = 1;
    b->xf_k = 0;
    freeze_live_grains(I);
}

void buffer_set_freeze(drz_inst_t *I, int on) {
    buffer_t *b = &I->buf;
    if (b->mode == BUF_MODE_SCAN) { /* Scan: Freeze only holds the tape (see buffer_block) */
        b->wait_full = 0;
        return;
    }
    b->wait_full = 0;
    if (on && !b->frozen) {
        if (b->valid < 4 * DRZ_XFADE) b->wait_full = 1; /* too little recorded yet (just after a load) */
        else start_freeze(I);
    } else if (!on && b->frozen) {
        b->frozen = 0;
        b->xf_k = DRZ_XFADE;
    }
}

/* The Live sweep: seconds since it started, as a phase 0..1 by sw_mode. */
static float live_sweep(const drz_inst_t *I) {
    const src_par_t *p = &I->sp_live;
    float x = I->src_live.sweep_s / p->sw_time;
    switch (p->sw_mode) {
    case SW_MODE_LOOP: return x - floorf(x);
    case SW_MODE_BOUNCE: {
        float t = x - 2.0f * floorf(x * 0.5f);
        return t <= 1.0f ? t : 2.0f - t;
    }
    default: return x < 1.0f ? x : 1.0f;
    }
}

/* One display bin of the Live ring per block, round robin: bin 0 is the oldest readable audio. */
static void live_bin(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    int32_t len = buffer_readable(I);
    int i = b->bin_next;
    b->bin_next = (i + 1) % DRZ_BINS;
    if (len < DRZ_BINS) {
        b->bins[0][i] = b->bins[0][DRZ_BINS + i] = 0;
        return;
    }
    int64_t start = (b->frozen ? (int64_t)b->ls + b->lf : b->w) - len + (int64_t)len * i / DRZ_BINS;
    int32_t span = len / DRZ_BINS;
    int32_t slot = (int32_t)(((start % DRZ_RING) + DRZ_RING) % DRZ_RING);
    float m = 0.0f, e = 0.0f, de = 0.0f, prev = I->data[0][slot];
    const float *d = I->data[0];
    for (int32_t k = 0; k < span; k++) {
        float x = d[slot], a = fabsf(x);
        if (a > m) m = a;
        e += x * x;
        de += (x - prev) * (x - prev);
        prev = x;
        if (++slot == DRZ_RING) slot = 0;
    }
    b->bins[0][i] = (uint8_t)(clampf(m, 0.0f, 1.0f) * 255.0f);
    b->bins[0][DRZ_BINS + i] = bright_byte(e, de);
}

static void live_block(drz_inst_t *I, int frames) {
    buffer_t *b = &I->buf;
    float *d = I->data[0];
    for (int done = 0; done < frames; done += DRZ_SUB) {
        int n = frames - done < DRZ_SUB ? frames - done : DRZ_SUB;
        float sw = live_sweep(I);
        I->src_live.sweep_s += (float)n / DRZ_FS;
        if (b->frozen) {
            if (b->xf_k >= DRZ_XFADE) continue; /* the source rests while frozen */
            source_render(&I->sp_live, &I->src_live, sw, I->track_semis, I->scratch, n);
            for (int i = 0; i < n && b->xf_k < DRZ_XFADE; i++, b->xf_k++) {
                int32_t slot = b->ls + b->xf_k;
                if (slot >= DRZ_RING) slot -= DRZ_RING;
                float a = (float)b->xf_k * (1.0f / (float)DRZ_XFADE); /* 0: all continuation */
                d[slot] = I->scratch[i] * sin_cycle(0.25f * a + 0.25f) + d[slot] * sin_cycle(0.25f * a);
            }
            continue;
        }
        source_render(&I->sp_live, &I->src_live, sw, I->track_semis, I->scratch, n);
        int32_t slot = (int32_t)(b->w % DRZ_RING);
        /* the source's pitch for these 16 samples: grains reading them later transpose from it */
        I->track16[slot / DRZ_SUB] = (int16_t)lrintf(clampf(I->track_semis, -300.0f, 300.0f) * 100.0f);
        for (int i = 0; i < n; i++) {
            d[slot] = I->scratch[i];
#ifdef DRZ_TEST
            I->tag[slot] = (uint32_t)(b->w + i + 1);
#endif
            if (++slot == DRZ_RING) slot = 0;
        }
        b->w += n;
        b->valid = b->valid + n > DRZ_RING ? DRZ_RING : b->valid + n;
        if (b->wait_full && b->valid >= buffer_len_samples(I)) {
            b->wait_full = 0;
            start_freeze(I);
        }
    }
    live_bin(I);
}

/* ------------------------------------------------------------------ Scan tapes */

static void scan_block(drz_inst_t *I, int frames) {
    buffer_t *b = &I->buf;
    int hold = I->v[P_BUF_FREEZE] >= 0.5f && b->front >= 0; /* Freeze in Scan: keep the tape */
    if (b->debounce > 0 && !hold) {
        b->debounce -= frames;
        if (b->debounce <= 0) {
            b->debounce = 0;
            b->rendering = 1;
            b->render_k = 0;
            b->render_len = buffer_len_samples(I);
            source_reset(&I->src_tape, (uint32_t)I->v[P_SRC_SEED]);
            bins_clear(b->bins[b->front < 0 ? 0 : 1 - b->front]);
            b->acc_e = b->acc_d = b->acc_prev = 0.0f;
            b->acc_bin = 0;
        }
    }
    if (!b->rendering) return;
    int back = b->front < 0 ? 0 : 1 - b->front;
    if (grains_reading(I, back)) return; /* grains still play the old tape: wait for them */
    float *d = I->data[back];
    uint8_t *bins = b->bins[back];
    int32_t len = b->render_len, todo = len - b->render_k;
    if (todo > DRZ_TAPE_CHUNK) todo = DRZ_TAPE_CHUNK;
    for (int32_t done = 0; done < todo; done += DRZ_SUB) {
        int n = todo - done < DRZ_SUB ? todo - done : DRZ_SUB;
        int32_t k = b->render_k;
        source_render(&I->sp, &I->src_tape, (float)k / (float)len, 0.0f, d + k, n);
        for (int i = 0; i < n; i++) {
            int bi = (int)((int64_t)(k + i) * DRZ_BINS / len);
            if (bi != b->acc_bin) { /* a bin is complete: its brightness */
                bins[DRZ_BINS + b->acc_bin] = bright_byte(b->acc_e, b->acc_d);
                b->acc_e = b->acc_d = 0.0f;
                b->acc_bin = bi;
            }
            float x = d[k + i];
            uint8_t a = (uint8_t)(clampf(fabsf(x), 0.0f, 1.0f) * 255.0f);
            if (a > bins[bi]) bins[bi] = a;
            b->acc_e += x * x;
            b->acc_d += (x - b->acc_prev) * (x - b->acc_prev);
            b->acc_prev = x;
        }
        b->render_k += n;
    }
    if (b->render_k >= len) {
        bins[DRZ_BINS + b->acc_bin] = bright_byte(b->acc_e, b->acc_d);
        b->tape_len[back] = len;
        b->front = back;
        b->rendering = 0;
        b->tape_renders++;
    }
}

void buffer_block(drz_inst_t *I, int frames) {
    if (I->sp.type == SRC_TYPE_SAMPLE) return; /* the file is the buffer (sample.c) */
    if (I->buf.mode == BUF_MODE_SCAN) scan_block(I, frames);
    else live_block(I, frames);
}
