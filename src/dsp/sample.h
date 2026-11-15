/* The Sample source (plan/04 section 5): WAV files read, mixed to mono, resampled to 44.1 kHz and
 * stored as int16 in an instance's smp[] by a worker thread, never on the audio callback. */
#ifndef DRZ_SAMPLE_H
#define DRZ_SAMPLE_H

#include <stdint.h>

#define DRZ_SMP_PATH 256

struct drz_inst;

/* Parse and decode a WAV file held in memory (tests, and the worker's chunks go through the same
 * decoder): mono int16 at the file's rate into out[0..cap). Returns frames, or < 0 with *err set. */
int32_t wav_decode(const uint8_t *data, int32_t len, int16_t *out, int32_t cap, int32_t *rate, const char **err);

/* Resample in[0..n) at `rate` to 44.1 kHz into out[0..cap), 16-tap windowed sinc. Returns frames. */
int32_t smp_resample(const int16_t *in, int32_t n, int32_t rate, int16_t *out, int32_t cap);

/* Scale x[0..n) so its peak is at -1 dBFS. */
void smp_normalise(int16_t *x, int32_t n);

/* --- per instance, from the audio thread ---------------------------------------------------------- */
void smp_attach(struct drz_inst *I);                      /* create_instance: start the worker, clear the slot */
void smp_request(struct drz_inst *I, const char *path);   /* set_param("smp_file") */
void smp_block(struct drz_inst *I);                       /* every block: acknowledge, pick up a finished load */
int smp_path(const struct drz_inst *I, char *buf, int cap);
int smp_status(const struct drz_inst *I, char *buf, int cap);

/* The browser build has no threads: the page decodes and resamples, and hands over 44.1 kHz floats. */
void smp_set_direct(struct drz_inst *I, const float *x, int32_t frames, const char *name);

#endif
