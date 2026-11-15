# 16 — Implementation Status

State of the code against this plan. Update it when the code changes; the code is the source of
truth.

## Done: M0 skeleton (November 2026), not yet run on a Move
| Area | Status |
|---|---|
| Repo files, vendored `plugin_api.h` and `move_info.h`, `module.json` (0.0.1), `help.json`, LICENSE, notices | Done |
| `scripts/build.sh` (native tests twice, contract, aarch64 `dsp.so`, `check_so.py`, package), `--bench`, `--render`, `--profile`; `deploy.sh`, `logs.sh` from Crest | Done, zig 0.17.1 |
| Instance pool (4), create/destroy, MIDI (notes, sustain, CC 69 freeze, 120/123), FTZ, soft clip, NaN guard, `cpu_us` | Done |
| Source: one Analog oscillator, Sine or 2-point polyBLEP Saw, at `src_root` | Done |
| Buffer: Live ring with the read guard, Freeze with the seam crossfade, freeze-after-load | Done |
| Grains: 128-grain pool, 64 per voice, Cloud scheduler with jitter, position/spray, Hermite reads in 32.32 fixed point with a wrap-free fast path, Hann window, spread, overlap gain | Done |
| Voices: 8, Poly, steal with a 64-sample grain fade, fixed 20 ms attack / 0.4 s release | Done |
| Params: 11 keys from plan/10, hand-written table; state blob; `chain_params` / `ui_hierarchy` built at init (Main + Source pages) | Done |
| Tests: 110 native checks under UBSan (ABI, params, state fuzz, render, pitch, level, guard fuzz, freeze, pool), run again on the slow read path; contract validator | Done |

Build output: `dsp.so` about 25 KB stripped, `drizzle-module.tar.gz` about 15 KB. `chain_params`
1.2 KB, `ui_hierarchy` 0.4 KB, state blob 243 B.

### Measured on the build machine (a cloud x86 VM, not a Move)
`scripts/build.sh --bench`, 3 000 blocks of 128 frames:

| Case | mean | p99 |
|---|---|---|
| idle | 0.9 µs | 3 µs |
| INIT, 8 notes (12 grains) | 8 µs | 35 µs |
| 8 notes, dense (128 grains) | 75 µs | 216 µs |
| 8 notes, pool full (128 grains) | 68 µs | 113 µs |
| 8 notes, pool full, pitched up | 109 µs | 185 µs |

The fast path took the full pool from 194 µs to 68 µs mean, about 4 ns per grain-sample here. The
VM's p99 and max are noisy. The device factor is unknown until `--profile` runs on a Move
([15 §2](15-risks-and-open-questions.md) Q4).

## Deviations from the spec
- **Live ring:** wraps at `DRZ_RING` (8 s), not at `buf_len`, so a length change keeps the recording
  ([05 §2](05-source-buffer.md), updated).
- **Freeze after a load** waits for the ring to record `buf_len` again instead of rendering it
  offline ([05 §3](05-source-buffer.md), updated).
- **Main page in M0:** Pos, Spray, Size, Dens, Jit, Len, Frz, Vol; Pitch Spray, Scan and Space
  arrive with their features. A Source page holds Root, Wave A (Sine/Saw) and Spread.
- **No codegen yet:** the parameter table and the contract are hand-written (`params.c`); the Hann
  table is built in `move_plugin_init_v2`. M1 generates both.
- **Amp envelope** is fixed (20 ms linear attack, 0.4 s exponential release) until M1's ADSR.
- **`release.json`** waits for the first tagged build.

## To verify on a Move (M0 acceptance)
1. `scripts/build.sh`, `scripts/deploy.sh`: Drizzle appears as DRZL and the log is clean; the
   ~26 MB `.bss` loads (Q1).
2. A pad plays a grain cloud; the Main page knobs work; CC 69 freezes (Q3).
3. Save to My Presets and reload the Set (Q2: the blob is 243 B now, about 4.5 KB at v1).
4. `scripts/build.sh --profile`, 8 held notes at Dens and Size 1: read `cpu_us` (Q4).

## Next
M1 ([14](14-roadmap.md)): `tools/gen`, Scan tapes, the full grain parameter set, Crest's
oscillators and FX, the WASM build.
