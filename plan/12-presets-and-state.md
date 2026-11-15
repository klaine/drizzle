# 12 — Presets & State

## 1. Requirements
As Crest: `get_param("state")` returns a complete blob of base values, `set_param("state")`
restores it, both without allocation or I/O and in bounded time, tolerant of unknown and missing
keys.

## 2. Blob
Flat JSON with string values, Crest's format, `"fmt":"drizzle","v":"1"`, every state key in the
generated order, the name (≤ 20 printable ASCII). Roughly 210 keys × ~20 B ≈ 4.5 KB (Crest's
blob, about 1.6 KB, fits; **VERIFY** the host buffer for a blob this size at M0, with Crest's
compact default-skipping format as the fallback).

**What the blob does not hold:** audio. A buffer is always rebuilt from the patch:
- **Scan:** the tape is re-rendered, and since it depends only on the patch, it is identical
  (synced Source LFO excepted: it follows the tempo at render time).
- **Live, frozen:** the frozen ring cannot be saved; on load the ring records `buf_len` of the source
  again and then freezes ([05 §3](05-source-buffer.md)). Close, not identical. Factory patches that
  start frozen use Scan instead.
- **Sample:** the file path; the file is loaded again ([04 §5](04-sources.md)).

**After parsing:** recompute derived values, snap smoothers, fade all voices and grains (64
samples), reset the buffer state (Live ring invalid, tape dirty with no debounce), clear the FX
incrementally.

## 3. Versioning
`"v"` bumps when semantics change; `params.c` holds a migration table; enums are append-only;
a blob whose `fmt` is not `drizzle` is ignored.

## 4. Factory bank
`presets/factory/NN_<category>_<name>.json`, each with only the keys that differ from INIT plus
`name` and `category`; `gen.py` validates and expands them into `factory_presets.c`
(binary `patch_t` per patch, Crest's method).

**The v1 bank: 65 patches with Init** (Pad got one more than planned), mostly ambient and texture,
with a rhythmic corner:

| Category | Count | Character |
|---|---|---|
| Init | 4 | Init (a wavetable tape), Init Live, Init Analog, Init Tempo |
| Pad | 15 | Slow clouds, Lock 0.3–0.7 for tone, wide spread, Hall or Cloud reverb, pressure → density |
| Texture | 12 | High spray, Vintage stepped tapes, noise and fold, reverse grains, level spray |
| Drone | 8 | Long tapes, very slow Scan, Lock 1 cores with sprayed halos, reverb freeze on Macro 4 |
| Keys & Bells | 8 | Perc windows, short envelopes, Fifth/Oct pitch spray, Plate |
| Motion | 6 | Sweep pitch tapes (swarms, glissandi), LFO 2 synced to Position, Live with `src_track` |
| Rhythm & Glitch | 12 | Tempo mode, repeats, probability, Bar random, Bits crush, ping-pong delay |

Rules for every factory patch (from Crest's and Amino's banks):
- Macros 1–4 do something audible: 1 motion, 2 texture/grit, 3 space, 4 the patch's trick.
- Pressure and the mod wheel do something.
- Levels are matched: `tools/level/level.py` sets each patch's `patch_vol` so the mean of the four
  loudest 250 ms slices of its C minor chord (the golden render) is within 1.5 dB of INIT's, unless
  that would put its peak above −4 dBFS: percussive and sparse patches are held at that ceiling,
  a few dB quieter (the tool marks them "peak-limited").
- Grains stay within +24 semitones of the root at the patch's intended range ([06 §4](06-grain-engine.md)).
- Golden-tested ([13 §3](13-testing.md)).

**Categories** in `params.py`: `CATEGORIES = [("init","Init"), ("pad","Pad"), ("texture","Texture"),
("drone","Drone"), ("keys","Keys & Bells"), ("motion","Motion"), ("rhythm","Rhythm & Glitch"),
("sample","Sample")]`. Sample patches arrive with M5 and need bundled audio, which must be our
own or CC0 ([00 §5](00-overview.md)); until then the category is unused.

**Authoring** happens in the browser build: the patch dialog's "dump state" writes the JSON
difference from INIT.

## 5. INIT patch
Wavetable source (table 0, waves A and B at 0, Vintage), Scan, 4 s tape with Sweep Timbre +1,
Position 0.5, Spray 0.1, Size ≈ 135 ms, Density ≈ 11/s, Jitter 0.5, Hann, Lock 0, pitch spray 0
with Oct quantise, spread 0.5, Filter Off, Amp A 0.3 s / D 0.5 s / S 0.8 / R 1.2 s, Hall reverb
at 0.3, Flavour 0.3, pressure → density 0.3 and → spray 0.2, volume 0.7. It sounds like a soft
cloud from the first note and every Main knob changes it audibly.
