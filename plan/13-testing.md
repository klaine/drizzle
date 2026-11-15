# 13 — Testing & Verification

Everything runs from `scripts/build.sh`; a red test blocks packaging. Structure copied from Crest.

## 1. Contract — `tests/contract/validate_contract.py`
Crest's validator: `chain_params` valid, < 31 KiB minified, unique keys, short names ≤ 5, short
options ≤ 4, enums ≤ 128 options, defaults in range; hierarchy keys exist, no selectors in `knobs`,
≤ 8 knobs, viz groups contiguous, every level reachable; `module.json` < 8 KB with the required
fields; `help.json` lines ≤ 20 ASCII. Plus `gen.py --check`.

## 2. Native tests — `tests/host/test_main.c`
Links `src/dsp/*.c`, drives the plugin through `move_plugin_init_v2` with a fake host (controllable
BPM, transport, beat position, set scale). UBSan always, ASan where zig supports it.

| Suite | Checks |
|---|---|
| ABI & pool | API version 2; 4 instances, the 5th NULL; destroy and recreate |
| Params | Every key: min, max, default round-trip; enums by name and index; NaN and inf rejected |
| State | 1 000 random patches round-trip byte-identically; malformed blobs never crash |
| Presets | All load; browser keys consistent; every patch renders non-silent and finite |
| Sources | Pitch and DC of both types; Vintage stepping; fold and tone bypass at their neutral values; Sweep and Source LFO reach their depths |
| Buffer | Live guard fuzz; freeze seam is click-free (no sample step > 4× the local RMS); tape determinism (two renders bit-identical); swap never changes a playing grain's buffer; debounce timing |
| Grains | [06 §8](06-grain-engine.md): timing, pitch, level, guard, pool, repeat and seed |
| Voices | [07 §6](07-voices-filter-amp.md) |
| Modulation | [08 §7](08-modulation.md) |
| FX | [09](09-effects.md) |
| Sample (M5) | WAV parser on 16/24/float, mono/stereo, odd rates, truncated and corrupt files; the loader handshake under a simulated slow worker; no grain ever reads `smp` while loading |
| Realtime | No allocation in entry points (wrap or `nm` check), bounded tape chunk and bin work per block |

## 3. Golden audio — `tests/golden/`
Amino's approach: `golden.json` covers **every factory patch** (C-minor triad, 3 s, then 1 s
release) plus tempo patterns at 120 BPM with a simulated clock, a Live/freeze case, a matrix-heavy
case and the CPU worst case. Fixed seeds make renders deterministic. `check_golden.py
build/drizzle_golden` compares against stored fingerprints (RMS per band and time slice, peak,
spectral centroid); `--write` updates them. An intended sound change needs `--write` and the
reason in the change description; never rewrite to make a failure go away. Golden tests run on
the host only.

## 4. `scripts/check_so.py`
`nm -D --undefined-only dsp.so` against a whitelist (Crest's, plus `pthread_*`, `fopen`, `fread`,
`fseek`, `fclose`, `opendir`, `readdir`, `closedir`, `sched_setaffinity` for the M5 loader).
`malloc`, `free`, `printf` and friends fail the build.

## 5. Benchmark — `tests/host/bench_main.c`
Per-stage timers under `-DDRZ_BENCH` (source, tape, grains, filter, control, FX). Cases: idle;
INIT with 8 notes; dense cloud (8 voices × 64 grains requested); worst case ([03 §7](03-dsp-architecture.md))
with a tape re-render forced every block. Report mean, p99 and max µs per block. The device
factor is calibrated at M0 with `--profile` on a Move.

## 6. UI tests — `tests/device/*.mjs`
Crest's mock canvas context: the home page and cards render for every factory patch, with
synthetic `buf_view` / `grain_view` answers, within the 128×64 bounds.

## 7. On-device checklist (each milestone)
1. `scripts/build.sh`, `scripts/deploy.sh`, rescan. Drizzle appears as **DRZL**; the log is clean.
2. Play pads: velocity, pressure on density and spray, sustain, freeze with CC 69 and the knob.
3. Walk every page; check cards, the home page animation, the matrix list, the browser.
4. Save to My Presets, reload the Set: a Scan patch sounds identical, a frozen Live patch close.
5. CPU: `--profile`, worst-case patch, 8 notes, all FX, tape re-render forced: `cpu_us` ≤ 900 p99.
   Then 4 tracks of Drizzle with Move's own instruments: no xruns.
6. Transport: Tempo mode, synced LFOs and delay follow Move's BPM and start/stop.

## 8. Listening checklist
- No zipper on any knob, including fast Position and Density turns.
- Density sweeps keep the level steady; no clicks at grain edges with Rect or Perc windows.
- Lock 1 sounds like a clean tone in tune; Lock 0 shimmers without beating at a fixed rate.
- Freeze, unfreeze, tape swaps and source-type changes are click-free.
- Tempo patterns sit on the grid with Move's sequencer.
- Pitch spray at Fifth/Oct over a held chord sounds consonant.
