# 14 — Roadmap

Each milestone ends with the on-device check ([13 §7](13-testing.md)) and a tagged build
`v0.M.0`. A box is ticked only when its acceptance criteria pass. Decisions taken so far are
in [15 §3](15-risks-and-open-questions.md).

## M0 — Skeleton & measurements
- [x] Repo files: `.gitignore`, `LICENSE`, `README.md`, `CLAUDE.md`, `BACKLOG.md`.
- [x] `THIRD_PARTY_NOTICES.md`, `CONTRIBUTING.md`. (`release.json` waits for the first tag.)
- [x] Vendor `plugin_api.h`, `move_info.h` from Crest. (`docs/_schwung_*.md` are local copies, git-ignored.)
- [x] Crest's `build.sh`, `deploy.sh`, `logs.sh`, `check_so.py` and contract validator, adapted. The web
      scripts (`build_web.sh`, `build_single.py`, `pages.sh`) move to M1 with the WASM build.
- [x] `drizzle.c`: pool of 4, an Analog saw/sine source at the root written into a Live ring, Freeze,
      8 voices of grains with Position, Spray, Size, Density, Jitter, FTZ, soft clip, `state`.
- [x] ABI, pool, params, state, guard, pitch, level and freeze tests; `--profile` (`cpu_us`) and the benchmark.
- [ ] **VERIFY on a Move:** `.bss` of ~26 MB loads; `state` buffer size; CC 69 reaches the slot;
      canvas poll rate; device-to-host speed factor. Record the answers in [15](15-risks-and-open-questions.md).

**Accept:** Drizzle loads on a Move, a pad plays a grain cloud of a saw, survives a Set reload, and
`cpu_us` is recorded.

## M1 — Grain engine, Scan tapes, codegen
- [x] `tools/gen/params.py` with every key in [10](10-parameter-reference.md) (inert keys allowed);
      `gen.py` adapted from Crest: `params_gen.*`, `contract_gen.c`, `windows_gen.c`, `factory_presets.c`,
      `build/ui/*.json`, `--check`.
- [x] `params.c`: get/set, state blob, factory bank and browser keys, `buf_view` / `grain_view`.
      (Smoothing beyond the master volume and the migration hook wait for a need: no release yet.)
- [x] Buffer: Live ring with the read guard, Freeze with the seam crossfade, Scan tape render at 8×
      with debounce and click-free swap, display bins.
- [x] Grains: pool, Cloud scheduler, position/spray/scan, Hermite reads, 6 windows, pitch and
      pitch spray with quantise, reverse, spread, level spray, gain normalisation, release thinning.
- [x] Sources: Analog (Crest's oscillators) with density, FM, sync and noise; fold, tone; Sweep.
- [x] Voices (Poly, voice limit, bend, octave), Amp envelope, FX chain ported from Crest.
- [x] Pages: Main, Source, Analog, Analog Mod, Sweep, Grains, Grain Pitch, Amp Env, Env Options,
      Voice, Crush, Chorus, Delay, Reverb, plus the factory browser.
- [x] **WASM site and single file** (`scripts/build_web.sh`, `build/drizzle.html`), with a patch-JSON export.
- [x] Contract, params, state, preset, source, buffer, scan, grain and FX suites; six starter patches.

**Accept:** an 8-voice granular synth on an Analog source in Live and Scan, fully editable on the
Move, state round-trips, `cpu_us` ≤ 500 µs with 8 voices and 64 grains.

## M2 — Wavetables, lock, character
- [x] Wavetable source: Amino's tables (copied from Amino's `tools/wavegen` output), Vintage and Clean, cross-mod.
- [x] Period lock; `src_track` in Live with the pitch stored per 16 samples written.
- [x] Source LFO; Wavetable pages.
- [x] Per-voice filter (stereo SVF from Crest), Mod envelope with direct depths, Env Options.
- [x] Voice modes Mono and Legato, glide, Flavour, `v_voices`; pitch quantise Scale from the Move set.
- [x] Golden harness: every factory patch plus four feature cases (`tests/golden`).
- [ ] Listening pass: if up-transposed grains alias badly, try the half-rate tape mip (R3). Needs ears.

**Accept:** worst case without FX ≤ 650 µs on the device; listening checklist items 1–4 pass.

## M3 — Rhythm & modulation
- [x] Tempo mode on the host clock (internal clock when stopped), swing, probability, repeats, Free/Note/Bar random.
- [x] LFO 1 and 2 with direct depths, pressure page, 12-slot matrix with list editor, macros.
- [x] Crush **Bits** (Crest's crush), reverb **Cloud** and **freeze**.
- [x] `:effective` live values; matrix, LFO, rhythm and reverb suites.

**Accept:** a 1/16 Tempo pattern stays on Move's grid for 5 minutes; "Wheel × LFO 2 → Position" and
"Poly AT → Density" work on the device; the full worst case ≤ 900 µs p99.

## M4 — UI & content
- [x] Home page canvas, cards, Patches browser, help (`src/canvas.js`, `src/cards.js`, `src/help.json`).
- [x] Remote UI panel with the buffer view and the shared preset browser (`src/web_ui.html`, `src/web/`).
- [x] 65 factory patches ([12 §4](12-presets-and-state.md)), levelled (`tools/level`), golden-tested.
- [ ] Device run-through: no unusable page, macros 1–4 audible on every patch. Needs a Move.

**Accept:** every patch loads click-free with macros 1–4 assigned; a blind run-through on the
device finds no unusable page.

## M5 — Sample mode
- [x] `sample.c`: WAV parser (PCM 16/24/32, float, extensible, 1–8 channels), 16-tap sinc resampler,
      loader thread with the per-slot handshake.
- [x] Sample page with the file browser, `smp_status`, the browser's file picker with IndexedDB.
- [x] `check_so.py` whitelist for the worker; sample suite (decoder, resampler, loader, swap while
      playing, missing file, state round trip).
- [ ] Sample category patches with our own or CC0 audio, if a good set exists. None yet.

**Accept:** a WAV copied to `UserLibrary/Drizzle` loads from the file browser without an audible
glitch in other tracks, survives a Set reload, and a missing file fails cleanly.

## M6 — Release
- [x] CI (`.github/workflows/build.yml`: build and test on every push, release assets on `v*` tags),
      `release.json` for 0.1.0, README with the feature list.
- [ ] Schwung catalog entry and release notes: after the device checks, with the owner.
- [ ] `pages` branch pushed (`scripts/pages.sh --push`): the owner's call, it publishes the site.

**Accept:** v1.0.0 installs through Schwung Manager.
