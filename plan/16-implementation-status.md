# 16 — Implementation Status

State of the code against this plan. Update it when the code changes; the code is the source of
truth.

## Built: M0–M6 (November 2026), version 0.1.0, not yet run on a Move

Everything in [14](14-roadmap.md) that does not need a Move or the owner is done. Nothing has run on
a Move yet, so the device questions of [15 §2](15-risks-and-open-questions.md) are still open and
gate v1.0.0.

| Area | Status |
|---|---|
| Codegen: `tools/gen/params.py` (217 keys) → C tables, contract, windows, wavetables, factory bank, web UI data, on-device META; `gen.py --check` in the build | Done |
| Sources: Wavetable (Amino's 74 tables, Vintage / Clean, ring / PM / sync), Analog (Crest's polyBLEP waves, density, FM, sync, noise), Sample (WAV via the loader thread); fold, tone; Sweep and Source LFO | Done |
| Buffer: Live ring with read guard and note tracking, Freeze with seam crossfade, Scan tapes at 8× realtime with debounce and click-free swap, Sample regions | Done |
| Grains: 128-grain pool, Cloud and Tempo scheduling, position / spray / scan, six windows, pitch, pitch spray (Off / Oct / Fifth / Scale), reverse, spread, level spray, period lock, probability, repeats, Random Free / Note / Bar, release thinning | Done |
| Voices: Poly / Mono / Legato, glide, Flavour, voice limit, bend, octave; per-voice stereo SVF; Amp and Mod envelopes | Done |
| Modulation: LFO 1 / 2 with direct depths, pressure, 12-slot matrix with list editor and proxies, 4 macros, `:effective` | Done |
| Effects: Crest's chain (Crush incl. Bits, Chorus, Delay, Reverb incl. Cloud), Reverb Freeze | Done |
| Move UI: 29 levels from `params.py`, home page and browser canvas, 34 cards, help | Done; drawn on a mock display only |
| Remote UI and browser synth (`src/web/`, `web/`), shared preset browser, single file `build/drizzle.html` (0.72 MB) | Done; checked in headless Chromium, standalone and with a mocked `schwungRemote` |
| Factory bank: 65 patches in 7 categories, macros and wheel on every patch, levelled (`tools/level`) | Done; no Sample patches (no audio of our own yet) |
| Tests: 189 native checks under UBSan on both read paths, contract, 314 device-UI checks, golden audio over 69 cases, wasm test; CI workflow | Done |

Build output: `dsp.so` 0.86 MB stripped (most of it the wavetables), package 0.55 MB. `chain_params` 22.1 KB,
`ui_hierarchy` 6.3 KB, state blob about 3.8 KB.

### Measured on the build machine (a cloud x86 VM, not a Move)
`scripts/build.sh --bench`, 128-frame blocks:

| Case | p99 |
|---|---|
| INIT, 8 notes | ≈ 35 µs |
| 8 notes, pool full (128 grains) | ≈ 130–160 µs |
| worst: pool full, LP24 on every voice, all effects, period lock | ≈ 140 µs |
| worst + a Scan tape re-rendering all the time | ≈ 220 µs |

Crest's estimate puts the Move's Cortex-A72 at 4–6× slower: the worst case may land near the
900 µs budget of [03 §7](03-dsp-architecture.md). The levers are listed there.

## Deviations from the spec
- **Live ring** wraps at 8 s whatever `buf_len` is; **freeze after a load** waits for the ring to
  refill ([05](05-source-buffer.md), updated).
- **Window edges** are fractions of the grain (3 %), not milliseconds ([06 §5](06-grain-engine.md), updated).
- **patch_vol** is gain 4·x² (0.5 = unity) instead of 2·x², for headroom to level the bank.
- **Levelling** uses the four loudest 250 ms slices under a −4 dBFS peak ceiling instead of LUFS;
  seven percussive or sparse patches are held at the ceiling, 1.5–8 dB under the rest.
- **Smoothing:** only the master volume is ramped. Grain parameters are latched per grain; source
  and effect knobs may zipper and are a listening item.
- **Unfreeze** can click where recording resumes (a listening item).
- **Bank size:** 65 with Init (15 pads, one more than planned).
- **Tempo mode** joins the grid on the step a note falls in if it came within the first quarter of
  it; a transport jump back re-syncs every voice.

## To verify on a Move (gates v1.0.0)
1. `scripts/deploy.sh`: Drizzle appears as DRZL, the log is clean, the ~26 MB `.bss` loads (Q1).
2. Walk every page; the home page draws the buffer and grains (Q5: poll rate); cards appear.
3. A Scan patch: the tape is audible within about 0.5 s; turning a source knob re-renders without a click.
4. CC 69 freezes (Q3); poly pressure moves the cloud; a Tempo patch follows Move's transport (Q7).
5. Save to My Presets and reload the Set (Q2: a 3.8 KB blob); a Sample patch reloads its file (Q6).
6. `scripts/build.sh --profile`, the worst-case patch, 8 notes: read `cpu_us` (Q4); then 4 tracks of
   Drizzle with Move's own instruments, listening for xruns.
7. Listen: the checklist of [13 §8](13-testing.md), aliasing of grains far above the root (R3), and
   a run through all 65 patches with their macros.

## Owner's decisions
- The browser synth is published on the `pages` branch (`scripts/pages.sh --push` updates it).
- Tagging v0.1.0 (CI then attaches the package and `drizzle.html` to the release) is still open.
- No Schwung catalog entry unless the owner asks for one.
