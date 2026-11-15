/* The source buffer, M0: the Live ring with Freeze (plan/05 sections 2-3). Scan tapes come at M1.
 *
 * The ring always wraps at DRZ_RING (8 s); buf_len only limits how far back grains read, so changing
 * it never scrambles what is already recorded. Nothing clears the ring: `valid` counts the samples
 * written since the last reset, and grains never read further back than that. */
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

/* Start over with nothing readable. The head keeps its absolute position, so grains still fading out
 * from before (a state load) keep reading what they meant to: new writes land ahead of them. */
void buffer_reset(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    int64_t w = b->w;
    memset(b, 0, sizeof *b);
    b->w = w;
    b->xf_k = DRZ_XFADE;
}

int32_t buffer_len_samples(const drz_inst_t *I) {
    float s = map_buf_len_s(I->v[P_BUF_LEN]) * DRZ_FS;
    int32_t n = (int32_t)s;
    return n < DRZ_RING ? n : DRZ_RING;
}

/* Freeze the most recent min(buf_len, valid) samples as a loop. The source keeps running for
 * DRZ_XFADE more samples, crossfaded (equal power) into the start of the loop, so the loop's end
 * runs on into its start without a seam (buffer_write_block). */
static void start_freeze(drz_inst_t *I) {
    buffer_t *b = &I->buf;
    int32_t lf = buffer_len_samples(I);
    if (lf > b->valid) lf = b->valid;
    b->lf = lf;
    b->ls = (int32_t)((b->w - lf) % DRZ_RING);
    b->frozen = 1;
    b->xf_k = 0;
}

void buffer_set_freeze(drz_inst_t *I, int on) {
    buffer_t *b = &I->buf;
    b->wait_full = 0;
    if (on && !b->frozen) {
        /* Too little audio for a loop yet (just after a load): freeze when there is. */
        if (b->valid < 4 * DRZ_XFADE) b->wait_full = 1;
        else start_freeze(I);
    } else if (!on && b->frozen) {
        b->frozen = 0;
        b->xf_k = DRZ_XFADE;
    }
}

void buffer_write_block(drz_inst_t *I, int frames) {
    buffer_t *b = &I->buf;
    float *d = I->data[0];
    for (int done = 0; done < frames; done += DRZ_SUB) {
        int n = frames - done < DRZ_SUB ? frames - done : DRZ_SUB;
        if (b->frozen) {
            if (b->xf_k >= DRZ_XFADE) continue; /* the source rests while frozen */
            source_render(I, I->scratch, n);
            for (int i = 0; i < n && b->xf_k < DRZ_XFADE; i++, b->xf_k++) {
                int32_t slot = b->ls + b->xf_k;
                if (slot >= DRZ_RING) slot -= DRZ_RING;
                float a = (float)b->xf_k * (1.0f / (float)DRZ_XFADE); /* 0: all continuation */
                float gin = sin_cycle(0.25f * a), gout = sin_cycle(0.25f * a + 0.25f);
                d[slot] = I->scratch[i] * gout + d[slot] * gin;
            }
            continue;
        }
        source_render(I, I->scratch, n);
        int32_t slot = (int32_t)(b->w % DRZ_RING);
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
}
