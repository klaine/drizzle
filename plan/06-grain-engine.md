# 06 — Grain Engine

Each voice runs a **scheduler** that decides when grains start, and draws grains from a shared
**pool** of 128. A grain is a windowed, possibly transposed and reversed read of the source buffer,
panned to a stereo position. Everything about a grain is decided at its onset.

## 1. Pool
- `grain_t grain[128]` with a free list. A voice may own up to `DRZ_VOICE_GRAINS = 64`.
- **Pool empty or voice at its cap:** the onset is skipped (never steal a playing grain: stealing
  clicks, skipping only thins the cloud). Test builds count skips.
- **Voice steal or all-sound-off:** the voice's grains fade over 64 samples (a per-grain fade
  counter), then return to the pool.
- **A voice stays active** until its amp envelope is idle **and** it owns no grains.

## 2. Scheduler
`gr_mode` picks **Cloud** or **Tempo**.

### Cloud
- Base interval `T = 1 / density`, `gr_dens` = 0.5..250 grains/s per voice (exponential).
- Jitter `j = gr_jit` (0..1): the next interval is `T · ((1 − j) + j · (−ln u))`, `u` uniform in
  (0, 1]. `j = 0` is perfectly periodic; at audio-rate densities that is a pulse train heard as a
  pitch of its own (useful, and documented in help). `j = 1` is a Poisson cloud.
- Onsets are fractional sample times: `sched_tick` spawns every grain whose onset falls in the
  current 16-sample sub-block and starts it at its own sample offset.
- The first grain of a note starts at note-on (no waiting for the first interval).

### Tempo
- Onsets on the grid `gr_div`: 1/64, 1/32T, 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4,
  1/4D, 1/2, 1 bar.
- **Clock:** with the transport running and `get_beat_position()` valid, steps are aligned to the
  host's beat. Otherwise an internal clock at `get_bpm()` (120 when unknown) starts at note-on.
  The beat position is read once per block and the step times inside the block are extrapolated
  from the BPM (Crest's arp does the same; [15](15-risks-and-open-questions.md) Q7).
- `gr_swing` 0..0.75 delays every second step by `swing × step / 2`.
- `gr_dens` is not used in Tempo; one grain starts per step.

### Shared by both modes
- **Probability** `gr_prob` 0..1: each onset sounds with this probability (in Cloud it thins the
  cloud; in Tempo it makes broken rhythms).
- **Repeats** `gr_rpt` 0..16: after a fresh onset, the next `rpt` onsets reuse its position,
  pitch, direction and pan (the classic stutter). Then a fresh one is drawn.
- **Random** `gr_rnd`: **Free** (the instance RNG), **Note** (each voice reseeds from
  `gr_seed` and the note at note-on: the same note plays the same pattern), **Bar** (reseeds from
  `gr_seed` at every bar start, so a Tempo pattern loops each bar; with the transport stopped, every
  4 beats of the internal clock). Note and Bar make glitch patterns repeatable and patch-defined.

## 3. Position
```
p = gr_pos + mod_pos + flavour_pos + scan_offset + spray_term + lock_correction
```
- **`gr_pos`** 0..1 of the readable range ([05](05-source-buffer.md): delay behind the head in
  Live, tape position in Scan, region position in Sample).
- **Spray** `gr_spray` 0..1: `spray_term = spray · rand_tri()` (triangular, −1..1, so most grains
  stay near the centre); spray 1 covers the whole buffer.
- **Scan** `gr_scan` −4..+4 (cubic knob curve, fine near 0): the voice's `scan_offset` moves by
  `scan × elapsed / readable_len`, so Scan 1 travels through the material at its own speed (a
  granular time-stretch at pitch-independent speed). It resets at note-on, except in Legato.
  Wraps in Live and Sample; bounces off the ends in Scan.
  In Live (not frozen) the position is already relative to the moving head, so Scan 0 plays new
  material as it arrives and Scan moves the delay.
- **Clamping:** the read guard of [05 §2](05-source-buffer.md) in Live; reflect off the ends in
  Scan; clamp to the region in Sample.

## 4. Pitch
```
semis = (voice_pitch − root) + gr_pitch + gr_fine/100 + q(pspray · rand_bi()) + mod_pitch
inc   = ±2^(semis/12) · (buffer_rate / 44100)
```
- `voice_pitch` includes glide, bend, octave and Flavour ([07](07-voices-filter-amp.md)).
  `root` is `src_root`, or in Live with `src_track` On the pitch the source had when the data was
  written (the voice that sets the source pitch hears its grains at `inc` ≈ 1, re-rendered rather
  than transposed).
- `gr_pitch` −24..+24 semitones, `gr_fine` ±50 cents.
- **Pitch spray** `gr_pspr` 0..24 semitones, quantised by `gr_pq`: **Off** (continuous),
  **Oct** (0 / ±12 / ±24), **Fifth** (octaves and fifths), **Scale** (the Move set's root and scale
  through `move_info.h`, as Crest's arp Scale = Set does; chromatic when unknown).
- **Reverse** `gr_rev` 0..1: probability that a grain reads backwards. A reverse grain plays the
  same segment as a forward one would, from its end.
- **Read step** is clamped to 1/64..16. Grains of a Scan tape keep their whole segment inside the
  tape: a grain longer than the tape allows at its pitch is shortened.
- **Range:** with no band-limiting on read, grains far above the root alias. Up to +24 semitones
  above `src_root` is the intended range (C3 root → C5); factory patches stay within it. A
  half-rate mip of the tape is an M2 experiment ([15](15-risks-and-open-questions.md) R3).

## 5. Window, size, pan, level
- **Size** `gr_size` 0..1. Cloud: 5 ms..2 s, exponential. Tempo: 1/16..4 steps, exponential, so
  gate length follows the tempo. The card shows ms or steps.
- **Window** `gr_win`: **Hann**, **Gauss** (σ = 0.18, lowered and rescaled to start and end at 0),
  **Tukey** (flat top, 25 % cosine edges), **Perc** (a cosine rise over 3 % of the grain, then an
  exponential fall to −60 dB), **Swell** (Perc reversed), **Rect** (3 % cosine edges). The edges
  are fractions of the grain, not milliseconds, so the six windows are fixed tables: 1 025 points
  plus a guard point in the generated `windows_gen.c`, linearly interpolated, with an RMS normaliser
  `DRZ_WIN_NORM` (at most 2) that gives each window Hann's loudness ([03 §5](03-dsp-architecture.md)).
- **Spread** `gr_spread` 0..1: pan = voice pan + `spread · rand_bi()`, equal-power.
- **Level spray** `gr_lspr` 0..1: grain level `1 − lspr · u` (accents and dropouts).

## 6. Period lock
`gr_lock` 0..1 makes grains phase-coherent, which turns a grainy cloud into a smooth tone
(pitch-synchronous granular).
- For a Wavetable or Analog source, the source period at the root, `P = 44100 / f(root)`, is known
  (with `src_track`, the period at write time, stored per bin). For Sample it comes from `smp_root`.
- Each voice keeps a reference phase `ref` that advances by `|inc|` per output sample, like the
  phase of a steady oscillator. At onset the start `p` moves to the nearest `p'` with
  `(p' − ref) mod P = 0`, and the grain starts at `p + lock · (p' − p)`.
- At 1, grains of the same pitch line up exactly, overlap-add without phasing, and the cloud sounds
  like the source played back at the note; at 0 their phases are random and the cloud shimmers.
  Pitch spray and Sweep pitch break the alignment by design.
- Gain normalisation moves from `1/√N` to `1/N` with `lock` ([03 §5](03-dsp-architecture.md)).

## 7. Rendering
For each grain of a voice, for each sample of the sub-block that the grain covers:
```c
x   = hermite4(buf, g->idx, g->frac);      // wrap (Live ring) or clamp (tape, sample)
w   = win_lut(g->win, g->win_pos);
acc_l += x * w * g->gl;  acc_r += x * w * g->gr;
advance(g->idx, g->frac, g->inc);  g->win_pos += g->win_inc;
```
Sample-mode grains read int16 and scale by 1/32768. Grains are rendered in a tight loop per grain
(not per sample across grains), so the compiler keeps the grain in registers. A grain whose
window ends inside the sub-block returns to the pool at once.

## 8. Tests
- **Timing:** Cloud with jitter 0 at 10 grains/s puts onsets 4 410 samples apart (±1); Tempo 1/16
  at 120 BPM puts them 5 512.5 ± 1 samples apart, aligned to a simulated beat clock.
- **Pitch:** a locked cloud (lock 1, spray 0) on a sine source at root, played at root + 7, has its
  fundamental within 1 cent of the expected frequency and no partial between harmonics above −50 dB.
- **Level:** sweeping density from 1 to 200 grains/s changes the RMS of a cloud by less than ±3 dB.
- **Guard:** fuzz Live reads (random pos, size, pitch, reverse, `buf_len`) and assert that no grain
  ever reads unwritten or overwritten samples (test builds tag each sample with its write block).
- **Pool:** 8 voices at maximum density and size never exceed 128 grains, never allocate, and skip
  rather than steal.
- **Repeat and seed:** with `gr_rnd` = Bar, two consecutive bars render bit-identically.
