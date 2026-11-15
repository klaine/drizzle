# 16 — Implementation Status

State of the code against this plan. Update it when the code changes; the code is the source of
truth.

## Done: M1 (November 2026), not yet run on a Move
M0 and M1 of [14](14-roadmap.md). Nothing has run on a Move yet, so M0's device questions
([15 §2](15-risks-and-open-questions.md) 1–4) are still open.

| Area | Status |
|---|---|
| Codegen: `tools/gen/params.py` (210 keys, every key of [10](10-parameter-reference.md)) → `params_gen.*`, `contract_gen.c`, `windows_gen.c`, `factory_presets.c`, `build/ui/*.json`; `gen.py --check` in the build | Done |
| Instance pool (4; 1 in the browser), MIDI (notes, sustain, bend, CC 69 freeze, 120/121/123), FTZ, soft clip, NaN guard, `cpu_us` | Done |
| Analog source: Sine / Tri / Saw / Pulse with Shape (Crest's polyBLEP waves), osc B, density (3 detuned copies), B→A phase modulation, sync with a polyBLEP-corrected reset, noise; fold; 12 dB tone filter | Done |
| Sweep: Timbre, Fold, Pitch and Tone depths with curve; across the tape in Scan, over `sw_time` (One / Loop / Bounce) in Live | Done |
| Buffer: Live ring with the read guard, Freeze with the seam crossfade and freeze-after-load; Scan tapes rendered at 8× realtime into a back buffer, 60 ms debounce, swapped only when no grain reads the buffer being written, held by Freeze; mode switching; display bins | Done |
| Grains: 128-grain pool, 64 per voice, Cloud scheduler, Position / Spray / Scan Speed, Hermite reads in 32.32 fixed point with a wrap-free fast path, six generated windows with RMS normalisers, pitch, fine, pitch spray quantised Off / Oct / Fifth, reverse, spread, level spray, overlap gain, release thinning | Done |
| Voices: Poly with a voice limit, octave, bend; Amp ADSR from Crest's env.c with velocity | Done |
| Effects: Crest's chain (Crush with Soft / Hard / Fold / Bits, Chorus, Delay, Reverb) in fixed order | Done |
| State blob (≈ 3.7 KB), factory bank (Init + 6 starter patches), factory browser keys (`preset`, `pb_*`), `buf_view`, `grain_view` | Done |
| Move pages: Main + 14 pages of M1 features + Factory Patches / Patch Category (host knob grid) | Done |
| Browser: `scripts/build_web.sh` → `build/site` and the single file `build/drizzle.html` (250 KB): all pages from `ui_meta.json`, live buffer and grain view, keyboard, Web MIDI, patch-JSON export | Done, checked in headless Chromium |
| Tests: 124 native checks under UBSan (twice: fast and slow read paths), contract validator, wasm test in Node | Done |

Build output: `dsp.so` 148 KB stripped, package 56 KB. `chain_params` 18.6 KB, `ui_hierarchy` 3.4 KB.

### Measured on the build machine (a cloud x86 VM, not a Move)
`scripts/build.sh --bench`, 3 000 blocks of 128 frames, INIT's reverb on:

| Case | mean | p99 |
|---|---|---|
| idle (reverb running) | 15 µs | 29 µs |
| INIT, 8 notes | 21 µs | 40 µs |
| 8 notes, dense (128 grains) | 74 µs | 112 µs |
| 8 notes, pool full, pitched up | 81 µs | 135 µs |

## Inert until their milestone
Stored in patches and shown where their page exists, but without effect yet:
- **M2:** the Wavetable source (`wt_*`; `src_type` Wavetable and Sample render the Analog source),
  `src_track`, Source LFO (`sl_*`), `gr_lock`, the filter (`flt_*`), the Mod envelope and its
  depths (`me_*`), `v_mode` Mono / Legato, `v_glide`, `v_flav`, pitch quantise Scale (rounds to
  semitones until the Move set's scale is read).
- **M3:** Tempo mode and the Rhythm keys (`gr_mode`, `gr_div`, `gr_swing`, `gr_prob`, `gr_rpt`,
  `gr_rnd`, `gr_seed`), LFO 1 / 2, pressure, the matrix and macros, reverb Cloud (plays Hall) and
  `rev_frz`; `:effective` answers the base value.
- **M5:** the Sample keys (`smp_*`).

## Deviations from the spec
- **INIT's source is Analog** until the Wavetable source exists (M2); then INIT moves to Wavetable as
  [12 §5](12-presets-and-state.md) says.
- **Window edges** are fractions of the grain (3 %), not milliseconds ([06 §5](06-grain-engine.md), updated).
- **Live ring** wraps at 8 s whatever `buf_len` is; **freeze after a load** waits for the ring to
  refill ([05](05-source-buffer.md), updated in M0).
- **Smoothing:** only the master volume is ramped. Grain parameters are latched per grain, so they
  need none; source and effect knobs may zipper and are a listening item.
- **Browser UI:** a standalone page generated from `ui_meta.json`, not yet the Remote UI on the Move
  with the shared preset browser (M4).
- **Starter patches** are not levelled (about 15 dB apart); levelling comes with the M4 bank.

## To verify on a Move
1. `scripts/build.sh`, `scripts/deploy.sh`: Drizzle appears as DRZL; the log is clean; the ~26 MB
   `.bss` loads (Q1).
2. The pages walk as in [11 §2](11-ui-pages.md); the factory browser loads the 7 patches.
3. A Scan patch: the tape is audible within about 0.5 s of loading; turning an Analog knob re-renders
   without a click.
4. CC 69 freezes a Live patch (Q3); save to My Presets and reload the Set (Q2: 3.7 KB blob).
5. `scripts/build.sh --profile`, 8 held notes on a dense patch: read `cpu_us` (Q4). M1's target is
   ≤ 500 µs with 8 voices and 64 grains.

## Next
M2 ([14](14-roadmap.md)): the Wavetable source from Amino's tables, period lock, `src_track`,
Source LFO, the per-voice filter, the Mod envelope, Mono / Legato / glide / Flavour, golden tests.
