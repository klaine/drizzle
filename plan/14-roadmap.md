# 14 — Roadmap

Each milestone ends with the on-device check ([13 §7](13-testing.md)) and a tagged build
`v0.M.0`. A box is ticked only when its acceptance criteria pass. Decisions taken so far are
in [15 §3](15-risks-and-open-questions.md).

## M0 — Skeleton & measurements
- [x] Repo files: `.gitignore`, `LICENSE`, `README.md`, `CLAUDE.md`, `BACKLOG.md`.
- [ ] `THIRD_PARTY_NOTICES.md` (with the vendored headers and wavetables), `CONTRIBUTING.md`, `release.json`.
- [ ] Vendor `plugin_api.h`, `move_info.h` from Crest; copy `docs/_schwung_*.md` locally.
- [ ] Copy and rename Crest's `scripts/` (build, deploy, logs, check_so, pages, build_web, build_single).
- [ ] `drizzle.c`: pool of 4, init, one Analog saw source at the root written into a Live ring, one
      grain voice reading it with fixed parameters, FTZ, soft clip, minimal `state`.
- [ ] ABI and pool tests; `--profile` and the sine benchmark for the device factor.
- [ ] **VERIFY on a Move:** `.bss` of ~26 MB loads; `state` buffer size; CC 69 reaches the slot;
      canvas poll rate; device-to-host speed factor. Record the answers in [15](15-risks-and-open-questions.md).

**Accept:** Drizzle loads on a Move, a pad plays a grain cloud of a saw, survives a Set reload, and
`cpu_us` is recorded.

## M1 — Grain engine, Scan tapes, codegen
- [ ] `tools/gen/params.py` with every key in [10](10-parameter-reference.md) (inert keys allowed);
      `gen.py` adapted from Crest: `params_gen.*`, `contract_gen.c`, `windows_gen.c`, `build/ui/*.json`, `--check`.
- [ ] `params.c`: get/set, dirty bits, smoothing, state blob, migration hook.
- [ ] Buffer: Live ring with the read guard, Freeze with the seam crossfade, Scan tape render at 8×
      with debounce and click-free swap, display bins.
- [ ] Grains: pool, Cloud scheduler, position/spray/scan, Hermite reads, 6 windows, pitch and
      pitch spray with quantise, reverse, spread, level spray, gain normalisation.
- [ ] Sources: Analog (Crest's oscillators), fold, tone; Sweep.
- [ ] Voices (Poly), Amp envelope, FX chain ported from Crest.
- [ ] Pages: Main, Source, Analog, Analog Mod, Sweep, Grains, Grain Pitch, Amp Env, FX.
- [ ] **WASM site and single file** early, so sound design can start.
- [ ] Contract, params, state, buffer and grain suites.

**Accept:** an 8-voice granular synth on an Analog source in Live and Scan, fully editable on the
Move, state round-trips, `cpu_us` ≤ 500 µs with 8 voices and 64 grains.

## M2 — Wavetables, lock, character
- [ ] Wavetable source: Amino's tables through `tools/wavegen`, Vintage and Clean, cross-mod.
- [ ] Period lock; `src_track` in Live with per-bin pitch.
- [ ] Source LFO; Wavetable pages.
- [ ] Per-voice filter (stereo SVF from Crest), Mod envelope with direct depths, Env Options.
- [ ] Voice modes Mono and Legato, glide, Flavour, `v_voices`.
- [ ] Golden harness with the first references.
- [ ] Listening pass: if up-transposed grains alias badly, try the half-rate tape mip (R3).

**Accept:** worst case without FX ≤ 650 µs on the device; listening checklist items 1–4 pass.

## M3 — Rhythm & modulation
- [ ] Tempo mode on the host clock, swing, probability, repeats, Free/Note/Bar random.
- [ ] LFO 1 and 2 with direct depths, pressure page, 12-slot matrix with list editor, macros.
- [ ] Crush **Bits**, reverb **Cloud** and **freeze**.
- [ ] `:effective` live values; matrix, LFO, rhythm suites.

**Accept:** a 1/16 Tempo pattern stays on Move's grid for 5 minutes; "Wheel × LFO 2 → Position" and
"Poly AT → Density" work on the device; the full worst case ≤ 900 µs p99.

## M4 — UI & content
- [ ] Home page canvas, cards, Patches browser, help.
- [ ] Remote UI panel with the buffer view.
- [ ] 64 factory patches ([12 §4](12-presets-and-state.md)), levelled, golden-tested.

**Accept:** every patch loads click-free with macros 1–4 assigned; a blind run-through on the
device finds no unusable page.

## M5 — Sample mode
- [ ] `sample.c`: WAV parser, resampler, loader thread with the per-slot handshake.
- [ ] Sample page, `smp_status`, the browser's drop-in loader with IndexedDB.
- [ ] `check_so.py` whitelist for the worker; sample suite.
- [ ] Sample category patches with our own or CC0 audio, if a good set exists.

**Accept:** a WAV copied to `UserLibrary/Drizzle` loads from the file browser without an audible
glitch in other tracks, survives a Set reload, and a missing file fails cleanly.

## M6 — Release
- [ ] CI, `release.json`, Schwung catalog entry, release notes.
- [ ] `pages` branch with the single file; README with the feature list.

**Accept:** v1.0.0 installs through Schwung Manager.
