# 07 — Voices, Filter & Amp

## 1. Voices
Up to 8 voices, one per held note. A voice owns a grain scheduler, its grains, a stereo filter, an
Amp and a Mod envelope and LFO 1. All voices read the one shared buffer.

| Key | What |
|---|---|
| `v_mode` | **Poly**, **Mono** (newest note, retrigger), **Legato** (newest note, envelopes and scan continue; glide only between overlapping notes) |
| `v_voices` | Poly voice limit 1..8 (fewer voices = cheaper, and a way to keep dense patches in budget) |
| `v_glide` | 0..2 s, exponential |
| `v_bend` | Bend range 0..24 semitones |
| `v_oct` | −2..+2 octaves |
| `v_flav` | Flavour 0..1 (from Amino): per-note random offsets drawn at note-on, see below |

- **Allocation and stealing** follow Crest: a free voice first, else the oldest released, else the
  oldest held; a stolen voice's grains fade in 64 samples. A note retriggered on a sounding voice
  reuses it.
- **Flavour** draws four values per note from the instance RNG: position ±0.1·flav, pan
  ±0.5·flav, pitch ±15·flav cents, size ×2^(±0.5·flav). It makes repeated notes and chords differ
  slightly, which keeps pads alive. Under `gr_rnd` = Note it is reseeded too, so it repeats.
- **Sustain** (CC 64) holds note-offs, as in Crest.

## 2. Source pitch with `src_track` (Live)
The source plays the newest held note, gliding with `v_glide`. Buffer bins store the pitch the
source had when written, so a voice reading older material is transposed by the difference
([06 §4](06-grain-engine.md)). When all notes are released, the source holds its last pitch.

## 3. Filter (per voice, stereo)
Crest's SVF, run on L and R with shared coefficients.

| Key | What |
|---|---|
| `flt_type` | **Off**, LP24, LP12, BP12, HP12 |
| `flt_cut` | 0..1 → 20 Hz..20 kHz (exponential, Crest's mapping) |
| `flt_res` | 0..1, self-oscillates near 1 |
| `flt_drive` | In-loop drive 0..1 |
| `flt_env` | Mod envelope depth ±1 (±8 octaves) |
| `flt_key` | Key tracking 0..1 (1 = 100 %) |
| `flt_vel` | Velocity depth 0..1 |

Off is the default: much granular material is shaped in the source (`src_tone`) and by the FX.
Filter Off costs nothing.

## 4. Amp
- Amp envelope ADSR `ae_a`, `ae_d`, `ae_s`, `ae_r` (Crest's times and curves), `ae_vel` 0..1.
- The VCA follows the envelope at control rate with linear ramps over each sub-block.
- `v_pan` −1..1 places all voices; grain spread adds to it.
- Release: grains keep spawning during the release, at a density scaled by the envelope level
  when `gr_relth` (release thinning) is On. That gives tails that thin out rather than just fade.

## 5. MIDI details
- Note on with velocity 0 is a note off. Release velocity is a matrix source.
- Poly aftertouch is matched to the voice by note; channel aftertouch applies to every voice
  without poly AT in the last 0.5 s, so either kind of pressure works.
- CC 120 fades every voice and grain in 64 samples and clears nothing else (the buffer and FX tails
  remain, which a performer usually wants); CC 121 resets controllers; CC 123 releases all notes.

## 6. Tests
- Allocation, stealing, modes, sustain, `v_voices` limit (Crest's voice suite, adapted).
- Legato: overlapping notes keep envelopes, scan offset and grains; detached notes restart them.
- A stolen voice reaches silence in ≤ 64 samples with no step larger than 0.05.
- `src_track`: holding C3 then G3 in Live, the newest grains of the G3 voice read at `inc` 1 ± 0.001.
