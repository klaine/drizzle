# 02 — Build Toolchain (Zig as the C compiler)

Same toolchain as Crest: plain **C11** compiled with **`zig cc`** for the device, native tests and
WASM. Python 3 runs the generators and validators. Start by copying Crest's `scripts/` and
adapting the names.

## 1. Targets
| Target | Command (core flags) | Output |
|---|---|---|
| Device | `zig cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a72 -std=c11 -O2 -ffast-math -fno-finite-math-only -fPIC -shared -fvisibility=hidden -lm -lpthread` | `modules/sound_generators/drizzle/dsp.so` |
| Native test | `zig cc -std=c11 -O0 -g -fsanitize=undefined` (+ ASan where available) | `build/drizzle_test` |
| Golden / bench / render | `zig cc -std=c11 -O2 -ffast-math -fno-finite-math-only` | `build/drizzle_golden`, `build/drizzle_bench`, `build/drizzle_render` |
| Browser | `zig cc -target wasm32-wasi -mexec-model=reactor -O2 -DDRZ_WASM` | `build/site/drizzle.wasm` |

- **Warnings everywhere:** `-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter`.
- **Fast-math** with `-fno-finite-math-only` on device and in tests alike: the NaN guard in
  `render_block` depends on it.
- **Exports:** `-fvisibility=hidden`, `DRZ_EXPORT` on `move_plugin_init_v2`.
- **Threads:** `-lpthread` only for the sample loader (M5). `scripts/check_so.py` whitelists
  `pthread_*`, `fopen`/`fread`/`fclose`, `opendir`/`readdir` for the worker and fails on
  `malloc`/`free` anywhere ([13 §4](13-testing.md)).
- **WASM:** `-DDRZ_WASM` drops the `host_api_v1_t` offset static assert, and the sample loader
  becomes a function the page calls with the decoded file ([04 §5](04-sources.md)).

## 2. `scripts/build.sh`
```
scripts/build.sh              # gen --check, contract, UI tests, native tests, golden, dsp.so, check_so.py, package, build/drizzle.html
scripts/build.sh --gen        # run tools/gen/gen.py first
scripts/build.sh --no-test    # device build + package only
scripts/build.sh --profile    # dsp.so with the read-only cpu_us key
scripts/build.sh --bench      # build/drizzle_bench: worst-case timing per stage
scripts/build.sh --render     # one WAV per factory patch in build/demo
scripts/build.sh --release    # publishable package, checked by scripts/check_release.py
```
Steps follow Crest's: find zig (`$ZIG`, `PATH`, `python3 -m ziglang`), gen check, contract,
`tests/device/*.mjs` UI tests, native tests, golden, device build, ELF and symbol checks, package
with `COPYFILE_DISABLE=1 tar --owner=0 --group=0 --no-xattrs`.

## 3. Web
`scripts/build_web.sh` builds `drizzle.wasm` and `build/site/` (AudioWorklet player, WebMIDI, the
Remote UI panel driving the wasm). `--single` makes `build/drizzle.html` with
`scripts/build_single.py`, which inlines the wasm and UI. The single file is the shareable demo
and the factory-patch authoring tool. `scripts/pages.sh [--push]` puts it on the `pages` branch as
`index.html`.

## 4. Deploy
`scripts/deploy.sh [host]` stages the package on the Move's data partition and renames it into
place, as Crest's does. `scripts/logs.sh` follows Schwung's debug log.

## 5. Repository layout
```
drizzle/
  plan/                     these documents
  docs/_schwung_*.md        vendored Schwung refs (git-ignored)
  src/
    module.json help.json web_ui.html canvas.js cards.js
    dsp/
      drizzle.c drizzle.h   entry points, instance pool, render loop, master
      source.c source.h     Wavetable and Analog sources, fold, tone, Sweep, Source LFO
      buffer.c buffer.h     Live/Freeze ring, Scan tape render and swap, overview bins
      grain.c grain.h       scheduler, pool, windows, interpolation
      voice.c voice.h       allocation, modes, glide, flavour, per-voice filter + VCA
      filter.c filter.h     SVF from Crest
      env.c lfo.c mod.c     envelopes, LFOs, matrix and direct depths
      fx.c fx.h             Crush, Chorus, Delay, Reverb (from Crest)
      sample.c sample.h     M5: WAV parse, resample, loader thread
      params.c              get/set, state, special keys
      dsp_util.h            fast math, smoothing, RNG, FTZ
      plugin_api.h move_info.h                       vendored
      params_gen.* contract_gen.c factory_presets.c wavetables.c windows_gen.c   GENERATED
    web/                    Remote UI (presetbrowser.* are byte-identical copies of Amino's)
  tools/
    gen/params.py gen.py    parameter source of truth + generator
    wavegen/                Amino's table recipes, reduced to the tables Drizzle uses
  presets/factory/*.json
  tests/contract tests/host tests/golden tests/device
  web/                      wasm host, worklet, single-file embed
  scripts/
  modules/sound_generators/drizzle/   build output (git-ignored except .gitkeep)
```
Generated files are committed so a fresh checkout builds without Python; `gen.py --check` keeps
them honest.

## 6. CI & release
GitHub Actions at M6: setup-zig → `scripts/build.sh --release` → artifact. Tags `v*` attach
`drizzle-module.tar.gz` to the release and update `release.json` for Schwung Manager.
