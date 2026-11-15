# Drizzle: Implementation & Specification Plan

Drizzle is a granular synthesizer for Ableton Move, delivered as a Schwung sound-generator module
(`dsp.so` + `module.json`). It grains a built-in synth (a wavetable or virtual-analog source that
renders into a shared buffer), and later samples. These documents are the spec it is built against.
Once code exists it is the source of truth: if code and spec disagree, fix one of them in the same
change.

| # | Document | Contents |
|---|----------|----------|
| 00 | [Overview & requirements](00-overview.md) | Product goals, scope, non-goals, naming/IP rules |
| 01 | [Platform & ABI](01-platform-and-abi.md) | Move hardware, Schwung plugin API v2, realtime rules, `module.json` |
| 02 | [Build toolchain (Zig)](02-build-toolchain.md) | `zig cc` for device, native and WASM builds, scripts, packaging, deploy |
| 03 | [DSP architecture](03-dsp-architecture.md) | Signal flow, rates, memory layout, gain staging, CPU budget |
| 04 | [Sources](04-sources.md) | Wavetable and Analog source blocks, Sweep, Source LFO, Sample (M5) |
| 05 | [Source buffer](05-source-buffer.md) | Live, Freeze and Scan modes, tape rendering, swap, sample storage |
| 06 | [Grain engine](06-grain-engine.md) | Scheduler, grain pool, windows, pitch, Cloud and Tempo modes, period lock |
| 07 | [Voices, filter & amp](07-voices-filter-amp.md) | Note handling, pitch model, per-voice filter and VCA, MIDI |
| 08 | [Modulation](08-modulation.md) | Envelopes, LFOs, direct depths, pressure, 12-slot matrix, macros |
| 09 | [Effects](09-effects.md) | Crush, Chorus, Delay, Reverb with freeze (ported from Crest/Amino) |
| 10 | [Parameter reference](10-parameter-reference.md) | Every key, range, default, curve, enum list |
| 11 | [Move UI pages](11-ui-pages.md) | `ui_hierarchy`, page layouts, buffer display, list pages, help, Remote UI |
| 12 | [Presets & state](12-presets-and-state.md) | State blob, factory bank plan, codegen, migration |
| 13 | [Testing & verification](13-testing.md) | Contract, native, golden, WASM, device and CPU tests |
| 14 | [Roadmap](14-roadmap.md) | Milestones M0–M6 with tasks and acceptance criteria |
| 15 | [Risks & open questions](15-risks-and-open-questions.md) | Things to VERIFY on hardware, decisions log |
| 16 | [Implementation status](16-implementation-status.md) | What is built, measured and deviates from this spec |

## Sibling projects
- **Crest** (`klaine/crest`) is the primary template. Take the Zig build, `tools/gen` codegen with
  `--check`, the contract validator, the polyBLEP oscillators, the SVF filter, the effects chain,
  the canvas/cards UI, the Remote UI and single-file web build, the shared preset browser, and the
  worker-thread file loader (`peak_bank.c`) that the sample loader is modelled on.
- **Amino** (`klaine/amino`) provides the 8-bit wavetables and their generator, the Vintage/Clean
  wavetable oscillator, Flavour, dedicated aftertouch knobs, the "via" matrix slot, golden tests over
  every factory preset, and the effects Crest's were adapted from (`src/dsp/fx.c`).

## Conventions
- **VERIFY** marks a claim that must be confirmed on a device or in the Schwung source before code
  depends on it.
- Parameter keys are written like `gr_size`. The definitive list is [10](10-parameter-reference.md).
- Normalised values are written as `0..1`; physical units are given where the mapping matters.
- "Voice" means one held note. "Grain" means one windowed read from the source buffer. A voice
  owns many grains; all voices read the same buffer.
