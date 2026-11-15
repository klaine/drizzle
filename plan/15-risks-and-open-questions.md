# 15 — Risks & Open Questions

## 1. Risks
| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | CPU over budget with 8 dense voices, stereo LP24 and a tape render | Medium | High | Measure at M0/M1; levers in [03 §7](03-dsp-architecture.md): voice limit, smaller tape chunk, linear reads, lower grain cap |
| R2 | ~26 MB of `.bss` for 4 instances is too much next to Move's own use | Low–Med | High | VERIFY at M0. Halve the buffers (4 s synth, 15 s sample) or allocate sample storage only for 2 slots |
| R3 | Grains transposed far above the root alias audibly (no band-limiting on read) | Medium | Medium | Keep factory patches within +24 st; M2 experiment: a half-rate mip of the tape (+50 % memory) used when `inc > 2` |
| R4 | A static periodic source makes dull clouds | Medium | High | Sweep and Source LFO are first-class (M1), INIT sweeps timbre, help and factory patches teach motion |
| R5 | Tape re-render latency (0.5 s for 4 s) feels sluggish while turning source knobs | Medium | Low | Debounced partial renders show progress on the home page; Live mode for immediate response; raise the chunk if CPU allows |
| R6 | Level jumps between sparse and dense clouds | Medium | Medium | Overlap-aware normalisation with lock blend ([03 §5](03-dsp-architecture.md)), tested within ±3 dB |
| R7 | `state` too large for the host buffer | Low | High | VERIFY at M0; compact default-skipping format |
| R8 | Canvas polling of `buf_view` / `grain_view` is too slow or too costly | Medium | Low | Bins at a lower rate, fewer grains, or a static picture plus a position cursor |
| R9 | Sample loading disturbs other tracks (I/O, page faults on first touch of `smp`) | Medium | Medium | Worker off core 3, chunked reads; pre-touch pages on the worker before publishing |
| R10 | Tempo grains drift against Move's sequencer | Low | Medium | Align to `get_beat_position()` each block; test 5 minutes against a simulated clock |
| R11 | IP: resemblance to commercial granular instruments | Low | High | Generic wording, original patches and content ([00 §5](00-overview.md)) |

## 2. Open questions (VERIFY), with the milestone that answers each
1. **[M0]** Does a 26 MB `.bss` `dsp.so` load, and what is Move's free memory with a full set?
2. **[M0]** Host buffer size for `get_param("state")` (blob ≈ 4.5 KB).
3. **[M0]** Does CC 69 reach a chain sound generator, and does Move send anything on it itself?
4. **[M0]** Device-to-host speed factor (Crest estimated 4–6×; measure it).
5. **[M4]** Canvas poll rate and the cost of a ~300-byte `get_param` answer per frame.
6. **[M5]** Is `/data/UserData/UserLibrary/Drizzle` the right place, and does Schwung's `filepath`
   browser show `.wav` files there as it shows Crest's `.syx`?
7. **[M3]** Is reading `get_beat_position()` once per block precise enough for 1/64 grains, or is
   an internal PLL needed (Crest's arp question)?

## 3. Decisions log
| Date | Decision |
|---|---|
| 2026-11-15 | Repository `klaine/drizzle`, module name Drizzle, id `drizzle`, abbreviation DRZL. |
| 2026-11-15 | One shared source buffer for all voices (paraphonic source, polyphonic grains); per-voice sources are not planned for v1. |
| 2026-11-15 | Synth resynthesis first (Wavetable and Analog sources, M1–M2); sample mode second (M5). |
| 2026-11-15 | Factory bank mostly ambient and texture, with a Rhythm & Glitch category (12 of 64). |
| 2026-11-15 | Crest's toolchain, codegen, filter, FX and UI; Amino's wavetables, Flavour, pressure page, "via" matrix and golden tests over every patch. |
| 2026-11-15 | Fixed FX order Crush → Chorus → Delay → Reverb; no `visible_if`; Filter Off by default. |
