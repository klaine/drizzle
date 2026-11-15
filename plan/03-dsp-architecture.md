# 03 — DSP Architecture

## 1. Signal flow
```
          ┌──────── shared, one per instance ────────┐
          │ Source (Wavetable | Analog | Sample*)    │      * M5
          │  osc A + osc B → fold → tone (mono)      │
          │  Sweep + Source LFO move timbre/fold/    │
          │  pitch/tone                              │
          └───────────────┬──────────────────────────┘
                          │ Live: 1× realtime write      Scan: 8× realtime tape render
                          ▼                              into the back buffer, then swap
                 ┌──────────────────┐
                 │  Source buffer   │  0.5–8 s mono float (Sample: int16 up to 30 s)
                 └────────┬─────────┘
          ┌───────────────┼───────────────────────────┐ reads only
          ▼               ▼                           ▼
 ┌─ voice 1 ───────────────────────────┐   … up to 8 voices (one per held note)
 │ scheduler → grains (from pool of 128)│
 │  window · Hermite read · pan  → L/R  │
 │  → SVF L/R (drive) → VCA (Amp env)   │
 └──────────────────┬───────────────────┘
                    ▼
        Σ voices (stereo) → Crush → Chorus → Delay → Reverb (freeze) → Volume → soft clip → int16
```
- **One source, many readers.** The source renders once per block (Live) or into a tape (Scan),
  never per voice. That is what keeps eight dense clouds affordable.
- **Grains are mono reads, panned per grain**, so everything after the grains is stereo.
- **FX are global** and stereo, processed per 128-frame block, as in Crest.

## 2. Rates and block structure
| Rate | Value | Used for |
|---|---|---|
| Audio | 44 100 Hz | Source (Live), grain reads, filter, VCA, FX |
| Control (sub-block) | every 16 samples = 2 756 Hz | Envelopes, LFOs, matrix, filter coefficients, scheduler parameters |
| Grain onset | sample-accurate (fractional) | Each grain starts at its exact sample; parameters are latched at onset |
| Block | 128 samples | Smoothing targets, tape chunk, tempo read, display bins |

```c
void render_block(inst, out, frames) {
    ftz_enter(&saved);
    params_apply_dirty(inst);                 // targets → smoothers; tape_dirty debounce
    tape_step(inst);                          // Scan: render ≤ DRZ_TAPE_CHUNK samples into back buffer
    tempo_read(inst);                         // bpm, beat position (guarded), bar phase
    source_live(inst, frames);                // Live mode, not frozen: write 128 samples at the head
    for (sb = 0; sb < frames; sb += DRZ_SUB) {        // DRZ_SUB = 16
        global_mod_tick(inst);                // LFO 2, Source LFO, macros, wheel, AT, bend
        for each active voice v:
            voice_control_tick(v);            // envs, LFO 1, matrix → per-voice targets
            sched_tick(v, sb);                // spawn grains whose onset falls in this sub-block
            grains_render(v, vbus, DRZ_SUB);  // every grain of v adds into vbus L/R
            voice_post(v, vbus, bus + 2*sb);  // SVF L/R, VCA, add into the bus
    }
    fx_process(inst, bus, frames);
    master_out(inst, bus, out, frames);       // volume, soft clip, NaN guard, float → int16
    ftz_exit(saved);
}
```
- **Source before voices.** The Live write for this block happens before any grain reads, and
  grains never read the 128 samples just written (the read guard in [05 §2](05-source-buffer.md)
  keeps them at least one block behind the head).
- **Latching.** A grain copies its position, size, pitch ratio, window, direction, pan and level at
  onset and keeps them; only the voice's filter, VCA and the global FX move under it. This is
  cheap and is how granular instruments normally behave.

## 3. Data layout
```c
#define DRZ_VOICES          8
#define DRZ_GRAINS        128      // shared pool
#define DRZ_SUB            16
#define DRZ_MAX_INSTANCES   4
#define DRZ_BUF_MAX   (8 * 44100)  // synth buffer, samples
#define DRZ_SMP_MAX  (30 * 44100)  // sample storage, samples (M5)

typedef struct {
    int32_t  idx;  float frac;      // read position: integer + fraction (float alone loses precision past 2^20)
    float    inc;                   // read step per output sample (signed for reverse)
    uint32_t age, len;              // samples played / total
    float    win_pos, win_inc;      // window phase 0..1
    float    gl, gr;                // pan × level × normalisation, latched
    uint8_t  voice, win, buf, live; // owner, window, which buffer (A/B/sample), Live-relative
} grain_t;

typedef struct drz_voice {
    uint8_t  active, gate, note, vel, rvel;  uint32_t age;
    float    pitch_cur, pitch_tgt, glide_coef;
    float    flav[4];                         // Flavour offsets: position, pan, pitch, size
    sched_t  sched;                           // next onset, repeat counter, rhythm step, rng
    svf_t    flt[2];                          // L, R
    env_t    amp, mod;  lfo_t lfo1;
    float    polyat, mod_val[MOD_SRC_COUNT], dst_acc[MOD_DST_COUNT];
    ramp_t   r_gain, r_cut;
    uint8_t  n_grains;
} drz_voice_t;

typedef struct drz_inst {
    _Atomic int in_use;
    patch_t   patch;  smooth_t sm[PARAM_SMOOTH_COUNT];  uint64_t dirty[PARAM_WORDS];
    source_t  src;                            // oscillators, fold, tone, sweep, source LFO
    buffer_t  buf;                            // head, len, mode, freeze, tape state, view bins
    float     data[2][DRZ_BUF_MAX];           // A/B: Live ring uses A; Scan front/back swap
    int16_t   smp[DRZ_SMP_MAX];               // M5 sample storage (copied in by the worker)
    grain_t   grain[DRZ_GRAINS];  uint8_t free_list[DRZ_GRAINS];  int n_free;
    drz_voice_t voice[DRZ_VOICES];
    lfo_t     lfo2;  mod_global_t gmod;  notestack_t held;
    fx_t      fx;
    float     bus[2 * 256];
    uint32_t  rng;  int preset_idx;  char name[24];
} drz_inst_t;

static drz_inst_t g_pool[DRZ_MAX_INSTANCES];   // .bss; no malloc
```

| Item | Size per instance |
|---|---|
| Synth buffers A + B (2 × 8 s float) | 2.82 MB |
| Sample storage (30 s int16 mono, M5) | 2.65 MB |
| Delay + reverb + chorus (from Crest) | ≈ 1.0 MB |
| Grains, voices, rest | < 40 KB |
| **Total** | **≈ 6.5 MB** |
| **Pool of 4** | **≈ 26 MB of `.bss`** |

`.bss` costs nothing in the file, and pages that are never touched are never resident (an
instance that never loads a sample never touches `smp`). **VERIFY** that 26 MB of `.bss` loads
without trouble next to Move's own memory use; the fallback is a 4 s synth buffer and a 15 s sample
limit, which halves both.

## 4. Smoothing & dirty handling
As in Crest: `set_param` writes the patch field and a dirty bit only; a write equal to the current
value is ignored (Scenes resend every block). `params_apply_dirty` runs once per block:
- **Continuous** values ramp over 1 block (4 for `flt_cut`, FX times and `gr_pos`).
- **Discrete** values apply at once. A window change applies to new grains only, so it never clicks.
- **Source values** (any key in the `src`, `wt_`, `an_`, `sw_`, `sl_` groups) also restart the
  tape debounce in Scan mode ([05 §4](05-source-buffer.md)).
- **Scheduler values** are read at each onset; there is nothing to ramp inside a grain.

## 5. Gain staging
- **Source** peaks at ±1 (osc A + B scaled by `1/(1 + level_b)`), before fold and tone.
- **Grains overlap.** A voice's grain sum grows with the overlap `N = density × size`. Each grain's
  gain is `w_norm / sqrt(max(N, 1))` for incoherent clouds, moving towards `w_norm / max(N, 1)` as
  period lock (`gr_lock`) rises, because locked grains add coherently. `w_norm` equalises the
  windows' RMS. `N` is computed at onset from the smoothed density and size, so a density sweep
  changes level smoothly, not stepwise.
- **Per voice:** a fixed `0.35` after the VCA. Never normalise by the number of voices (Crest's and
  SchwungFreak's lesson: held notes duck).
- **Master:** `patch_vol` (`x²` curve, ×2), the soft clipper from Crest, ×32767 with saturation.
- **NaN guard:** one finiteness check of the bus per block; on failure zero the bus, reset filter
  and FX state, and count it (`nan_resets` in test builds).

## 6. Shared utilities (`dsp_util.h`)
Copied from Crest: `fast_tanh`, `fast_exp2`, `pitch_to_ratio(semis)`, the polynomial sine,
polyBLEP/BLAMP, xorshift32 RNG with `rand_uni()`/`rand_bi()`/`rand_gauss()` (sum of 4 uniforms),
`ftz_enter`/`ftz_exit`, linear ramps. New:
- **Window tables** (`windows_gen.c`, generated): 6 shapes × 1025 points, linearly interpolated.
- **4-point Hermite** read with wrap (ring) or clamp (tape, sample).

## 7. CPU budget
Block period 2 902 µs; device slack about 2 370 µs shared with other modules. Target for the
worst case (8 voices, 128 grains, Hermite reads, LP24 on all voices, all FX on, a tape re-render
in progress) is **≤ 900 µs**; a typical pad **≤ 450 µs**. Estimates scale Crest's measurements
(Crest's 8-voice, 3-oscillator worst case was 43 µs on Apple silicon) by 4–6× for the A72.

| Stage | Budget (worst) | Notes |
|---|---|---|
| Source, Live (2 osc, fold, tone, 128 samples) | 25 µs | Vintage wavetable at 4× oversampling is the dear case |
| Tape render chunk (1 024 samples = 8× realtime) | 160 µs | Only while a re-render runs; `DRZ_TAPE_CHUNK` tunes it |
| Grains (128 × 128 samples: Hermite + window + pan) | 200 µs | ≈ 12 ns per grain-sample on the A72 |
| Voice filters (8 × stereo LP24 = 32 SVF stages) | 220 µs | 12 dB modes and Off cost half or nothing |
| Control (envs, LFOs, matrix, scheduler) | 40 µs | 8 sub-blocks × 8 voices × 12 slots |
| FX (crush, chorus, delay, reverb) | 230 µs | Crest's chain; reverb is most of it |
| Master | 10 µs | |
| **Total** | **≈ 885 µs** | |

Levers, in order:
1. Skip idle voices and free grains (a voice with no grains and an idle amp envelope costs nothing).
2. Filter Off is a bypass; the 12 dB modes run one stage per channel.
3. Lower the tape chunk to 512 samples (4× realtime) when the profile build shows pressure.
4. Linear instead of Hermite reads for grains with `|inc| ≤ 1.01` and no lock (inaudible there).
5. A global grain cap below 128 (`DRZ_GRAINS` is a constant; the scheduler already skips onsets
   when the pool is empty).
6. Effects at level 0 switch off after 2 s, as in Crest.

Measure on the host with `scripts/build.sh --bench` and on the device with `--profile`
([13 §5](13-testing.md)).
