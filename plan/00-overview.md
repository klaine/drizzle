# 00 — Overview & Requirements

## 1. Product statement
Drizzle is a creative granular synthesizer for Ableton Move. Instead of asking the player to bring
samples, it carries its own sound source: a small synth (8-bit wavetables or virtual analog) that
renders into a **shared source buffer**. Each played note becomes a cloud of grains read from that
buffer at the note's pitch. Sample playback is a second source type that arrives later (M5).

The sound it is built for, in order of priority:
1. **Ambient pads and textures:** slow, wide, evolving clouds; frozen buffers scanned by LFOs;
   long reverb.
2. **Drones and timbre scans:** a "tape" of the source sweeping through its wavetable, read back
   as a spectral cross-section.
3. **Rhythmic stutter and glitch:** tempo-synced grain triggers, repeats, probability, reverse
   grains, locked random patterns that repeat every bar.

Drizzle runs entirely inside Move as a Schwung chain sound generator. Every page is editable with
Move's 8 encoders, and the pads' pressure plays the grain cloud.

## 2. Goals
1. **Sound.** Smooth clouds without zipper, clicks or DC; tonal grains that stay in tune; a source
   that is interesting on its own and more interesting when scanned.
2. **Playable.** A Main page that makes sound design possible without menu diving: Position,
   Spray, Size, Density, Pitch Spray, Scan, Space and Freeze. Pressure, velocity and the mod wheel
   do something in every factory patch.
3. **Realtime-safe and frugal.** The worst-case patch stays within **900 µs** of each 2.9 ms block
   on core 3 ([03 §7](03-dsp-architecture.md)).
4. **Move-native UX.** 2×4 pages, the same knob doing the same job on sibling pages, a home page
   that shows the buffer and the live grains.
5. **Robust state.** Patches survive Set reloads, My Presets and version upgrades. A frozen or
   scanned buffer comes back from the patch alone.
6. **Fast iteration.** Codegen keeps C tables, the UI contract and presets in sync. Native, golden
   and WASM builds make most work possible without a device; the browser build is the sound-design
   tool from M1 on.

## 3. Non-goals (v1)
- Audio input. A sound generator has none; a granular audio effect is a separate module
  (see `BACKLOG.md`).
- Per-voice sources. The source and its buffer are shared by all voices (decision 2026-11-15).
  Voices differ in pitch, grain parameters, filter and envelopes, not in the material they read.
- Recording a sample on the Move. Sample mode reads files the user copies to
  `UserLibrary/Drizzle` ([04 §5](04-sources.md)).
- MPE beyond polyphonic aftertouch and pitch bend. Multitimbral operation.
- Spectral (FFT) resynthesis. "Resynthesis" here means rendering a synth into a buffer and
  re-reading it as grains, not analysis/resynthesis.

## 4. Feature checklist (v1.0 scope)

| Area | Feature | Spec |
|------|---------|------|
| Sources | Wavetable (2 × 8-bit tables from Amino, Vintage/Clean, cross-mod), Analog (2 × polyBLEP osc from Crest, shape, sync, FM, density, noise), fold, tone; Sample from M5 | [04](04-sources.md) |
| Source motion | Sweep (a curve across the tape or over time) and Source LFO, each with Timbre, Fold, Pitch and Tone depths | [04 §4](04-sources.md) |
| Buffer | 0.5–8 s, Live (continuous write), Freeze, Scan (an offline tape rendered at 8× realtime and swapped without clicks) | [05](05-source-buffer.md) |
| Grains | 128-grain pool; Position, Spray, Size, Density, Jitter, Scan speed, 6 windows, period lock, pitch, pitch spray with quantise, reverse probability, stereo spread, level spray | [06](06-grain-engine.md) |
| Rhythm | Cloud or Tempo mode, 1/64–1 bar divisions with triplets and dots, swing, probability, repeats, random Free / Note / Bar | [06 §6](06-grain-engine.md) |
| Voices | 8 voices, Poly / Mono / Legato, glide, bend, octave, Flavour (per-note offsets), sustain | [07](07-voices-filter-amp.md) |
| Filter & amp | Per-voice stereo SVF (LP24, LP12, BP12, HP12, Off) with drive, Amp envelope | [07 §3](07-voices-filter-amp.md) |
| Modulation | Amp and Mod envelopes, LFO 1 (per voice), LFO 2 (global), direct depths, pressure page, 12-slot `src × via → dst` matrix, 4 macros | [08](08-modulation.md) |
| FX | Crush → Chorus → Delay → Reverb, tempo sync, reverb freeze | [09](09-effects.md) |
| State | Full-state blob, 64 factory patches in 7 categories, version migration | [12](12-presets-and-state.md) |
| UI | About 25 knob pages, home page with buffer and grain display, matrix list editor, cards, help | [11](11-ui-pages.md) |
| Web | Remote UI panel, WASM site and the shareable single-file `build/drizzle.html` | [11 §8](11-ui-pages.md) |

## 5. Naming & IP rules
- **Name.** The module is "Drizzle", id `drizzle`, abbreviation `DRZL`.
- **Wording.** User-facing text describes it as a "granular synth". It names no commercial granular
  instrument or hardware, and copies no presets, panel graphics or manuals.
- **Original content.** Wavetables come from Amino's generator and the CC0 AKWF-FREE set (same
  rules as Amino's [docs/07](https://github.com/klaine/amino/blob/main/docs/07-wavetables-and-ip.md)).
  Factory patches are original. Samples, if any ship, are made by our own scripts or are CC0
  with attribution in `THIRD_PARTY_NOTICES.md`.
- **Licence.** MIT, like Crest and Amino, with `LICENSE` in the package.
