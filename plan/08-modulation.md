# 08 — Modulation

The design follows Amino: the common moves have **direct depth knobs** on the page of the source
that makes them, so most patches never open the matrix. The matrix handles the rest.

## 1. Envelopes
- **Amp** (`ae_`) and **Mod** (`me_`) envelopes per voice: ADSR with Crest's ranges (attack
  0..20 s, decay 0..22 s, release 0..25 s, exponential curves), velocity depth.
- `me_` direct depths: `me_pos` (±1 position), `me_size` (±1 normalised size), `me_dens`
  (±1 = ±4 octaves of density), `me_pitch` (±24 semitones), plus `flt_env` on the Filter page.
- `me_trig`: **Note** (each voice) or **First** (restarts only when all keys were up; for slow
  evolutions shared across a chord).

## 2. LFOs
| | LFO 1 | LFO 2 |
|---|---|---|
| Scope | Per voice | Global |
| Keys | `l1_wave`, `l1_rate`, `l1_sync`, `l1_trig` | `l2_wave`, `l2_rate`, `l2_sync`, `l2_phase` |
| Direct depths | `l1_pos`, `l1_size`, `l1_pitch`, `l1_cut` | `l2_pos`, `l2_dens`, `l2_spray`, `l2_pan` |

- Waves: Sine, Tri, Saw Up, Saw Down, Square, S&H, Smooth.
- Rate 0.01..40 Hz (exponential), or synced: Off, 1/32..16 bars with triplets and dots (Crest's
  table, shortened to 20 entries).
- `l1_trig`: Free / Note (restarts at note-on) / Note+Random (random start phase per note).
- `l2_phase` sets where a synced LFO 2 sits in the bar (0..1), for patches whose motion must line
  up with the beat.
- LFOs run at control rate (2 756 Hz). Grain parameters only see them at onsets.

## 3. Pressure (direct)
Move's pads are pressure-sensitive and granular clouds respond well to pressure, so pressure has
its own page (Amino's lesson: if pressure needs the matrix, nobody uses it):

| Key | Depth |
|---|---|
| `at_pos` | Position ±1 |
| `at_dens` | Density ±1 (±4 octaves) |
| `at_spray` | Spray ±1 |
| `at_size` | Size ±1 |
| `at_pitch` | Pitch spray ±1 (±24 semitones of extra spray) |
| `at_cut` | Cutoff ±1 |

Poly AT per voice; channel AT as described in [07 §5](07-voices-filter-amp.md).

## 4. Matrix
12 slots of `source × via → destination × depth` (Amino's "via" form: the via source scales the
amount; None = 1).

**Sources (24):** None, Amp Env, Mod Env, LFO 1, LFO 2, Source LFO, Sweep, Velocity, Release
Velocity, Key (−1 at C1, +1 at C7), Poly AT, Channel AT, Pressure (whichever is newer), Mod Wheel,
Breath, Expression, Sustain, Bend, Macro 1–4, Random (per note), Voice Index.

**Destinations (32):**
- Grains: Position, Spray, Size, Density, Jitter, Scan, Pitch, Pitch Spray, Reverse, Spread,
  Probability, Level Spray, Lock
- Source (Live only, [04 §4.3](04-sources.md)): Timbre, Fold, Tone, Source Pitch, Osc B Level,
  Cross-mod / FM amount
- Voice: Cutoff, Resonance, Filter Drive, Level, Pan
- Modulators: LFO 1 Rate, LFO 2 Rate, Source LFO Rate
- FX: Crush, Chorus Level, Delay Feedback, Reverb Level, Reverb Size

Global destinations driven by per-voice sources take the newest voice's value.

**Editing** follows Crest's list pattern: `mm_items` lists the slots (`3: LFO2xWhl>Pos`), Matrix
Edit has `mm_src`, `mm_via`, `mm_dst`, `mm_dep`, `mm_slot`, `mm_clear` as proxies for the selected
slot. The slot keys themselves (`mm01_src` … `mm12_dep`) are state-only (`cp=False` in
`params.py`), which keeps them out of `chain_params`.

## 5. Macros
`mac1`..`mac4`, 0..1, are matrix sources and sit on the Perform page. Every factory patch gives
them meaningful jobs (convention: 1 = motion, 2 = texture/grit, 3 = space, 4 = the patch's own
trick). There is no separate macro-routing page; a macro's targets are matrix slots.

## 6. Live values
`gr_pos`, `gr_size`, `gr_dens` and `flt_cut` are `live:true`: their knob rings and the home page
show the modulated value of the newest voice through `<key>:effective`.

## 7. Tests
Routing maths per source and destination, via scaling, proxies against slots, items JSON validity,
and "Mod Wheel × LFO 2 → Position" moving the newest grain's position by the expected amount.
