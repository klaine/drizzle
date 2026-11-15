# 04 — Sources

One source per instance renders mono audio into the source buffer ([05](05-source-buffer.md)).
`src_type` picks Wavetable, Analog or (M5) Sample. Fold, tone and the two motion generators,
Sweep and Source LFO, are shared by the Wavetable and Analog types.

```
osc A ─┐
       ├─ mix (+ ring / PM / sync / FM) ─► fold (ADAA) ─► tone (12 dB LP) ─► buffer
osc B ─┘          ▲ noise (Analog)
Sweep, Source LFO ──► Timbre, Fold, Pitch, Tone
```

## 1. Common controls
| Key | What |
|---|---|
| `src_type` | Wavetable / Analog / Sample (M5). A change crossfades the whole cloud over 20 ms and, in Scan, re-renders the tape. |
| `src_root` | The note the source plays (0..127, default 48 = C3). Grains are transposed relative to it ([06 §4](06-grain-engine.md)). |
| `src_track` | Live mode only: Off, or On = the source plays the newest held note, with the voice glide ([07 §2](07-voices-filter-amp.md)). |
| `src_fold` | Wavefolder drive 0..1 (Amino's Fold, 1st-order ADAA from Crest). 0 bypasses it. |
| `src_tone` | Low-pass on the source, 12 dB SVF, 150 Hz..20 kHz on an exponential curve; 1 = open, bypassed. |
| `src_seed` | 0..999: seeds the source's noise and S&H so a tape is a pure function of the patch. |

The **Timbre** axis is what Sweep and Source LFO move. For Wavetable it offsets the wave position
of both oscillators (±1 = ±63 waves); for Analog it offsets both oscillators' Shape (±1 = full range).

## 2. Wavetable source (from Amino)
Two oscillators over Amino's 74 tables (64 waves × 128 signed 8-bit samples; waves 61–63 are
triangle, square and saw in every table). The tables, their generator and the CC0 AKWF waves come
from Amino's `tools/wavegen`, same licence rules ([00 §5](00-overview.md)).

| Key | What |
|---|---|
| `wt_a_table`, `wt_a_wave` | Osc A table and wave position 0..63 |
| `wt_b_table`, `wt_b_wave` | Osc B table and wave position |
| `wt_b_semi`, `wt_b_fine`, `wt_b_lvl` | Osc B interval (−24..+24), fine (±50 ct), level 0..1 |
| `wt_xmod`, `wt_xamt` | Cross-mod of B onto A: Off / Ring / PM / Sync, and its amount |
| `wt_q` | **Vintage** (no interpolation, 4× oversampled, stepped waves: the 8-bit sound) or **Clean** (band-limited copies, adjacent waves crossfaded) |

In Vintage the wave position steps through the 64 waves. A Sweep across the tape then makes 64
audible timbre zones, which Position and Spray pick between: stepped timbre is the point.

## 3. Analog source (from Crest)
Two of Crest's polyBLEP oscillators (2-point by default).

| Key | What |
|---|---|
| `an_a_wave`, `an_a_shape` | Osc A Sine / Tri / Saw / Pulse and Shape (Crest's mapping: triangle slope, pulse width, saw fold-over) |
| `an_b_wave`, `an_b_shape` | Osc B, same |
| `an_b_semi`, `an_b_fine`, `an_b_lvl` | Osc B interval, fine, level |
| `an_dens`, `an_ddet` | Osc A density (up to 4 detuned copies, Crest's supersaw) and its detune |
| `an_fm` | B → A phase modulation depth |
| `an_sync` | Osc A virtual sync ratio (1 = off .. 8) |
| `an_noise` | White noise level into the mix (fixed colour; Crest dropped the type knob for the same reason) |

## 4. Source motion
Without motion, a source plays one steady periodic wave, and grains of a periodic wave just give
the same wave back. Motion is what fills the buffer with timbres worth scanning.

### 4.1 Sweep (`sw_`)
A single curve from 0 to 1:
- **In Scan mode** it spans the whole tape: the start of the tape has the source at sweep 0, the end
  at sweep 1. Position then becomes a timbre axis.
- **In Live mode** it runs in time, over `sw_time` seconds, from the first note after all keys were
  up (or from load, when `src_track` is Off and the cloud is free-running).

| Key | What |
|---|---|
| `sw_time` | Live duration, 0.1..60 s (exponential). Ignored in Scan. |
| `sw_curve` | −1..1: log / linear / exponential |
| `sw_mode` | One (holds at 1) / Loop / Bounce |
| `sw_timb`, `sw_fold`, `sw_pitch`, `sw_tone` | Depths: Timbre ±1, Fold ±1, Pitch ±24 semitones, Tone ±1 |

A sweep with `sw_pitch` makes a glissando tape; Position then picks pitches and Spray makes
clusters, which is a deliberate source of chords and swarms.

### 4.2 Source LFO (`sl_`)
| Key | What |
|---|---|
| `sl_wave` | Sine / Tri / Saw / Square / S&H / Smooth (interpolated random) |
| `sl_rate` | 0.01..40 Hz (exponential), or `sl_sync` divisions |
| `sl_sync` | Off, or 1/16..8 bars. In Scan, sync divisions are measured at the patch tempo when the tape is rendered (`host->get_bpm()`, 120 when unknown) |
| `sl_timb`, `sl_fold`, `sl_pitch`, `sl_tone` | Depths, as for Sweep (pitch ±12) |

In Scan the LFO starts at phase 0 at the start of the tape and runs in tape time, so the tape is
deterministic. In Live it runs freely in real time.

### 4.3 What modulation reaches the source
- **Scan:** only the base values, Sweep and Source LFO. Matrix destinations in the source group act
  only in Live mode, because modulating a tape would mean re-rendering it continuously.
- **Live:** base + Sweep + Source LFO + the matrix's source destinations. Per-voice sources (a
  voice's envelope, LFO 1, velocity) drive a source destination through the **newest** voice.

## 5. Sample source (M5)
| Key | What |
|---|---|
| `smp_file` | Schwung `filepath` browser rooted at `/data/UserData/UserLibrary/Drizzle`, `.wav`, subfolders allowed |
| `smp_root` | The note the sample is recorded at (default 60) |
| `smp_start`, `smp_end` | The region grains read, 0..1 of the file; Position is mapped into it |
| `smp_norm` | Off / On: peak-normalise on load |

- **Formats:** RIFF WAV, PCM 16 / 24-bit or float32, mono or stereo (summed to mono), any sample
  rate (resampled to 44.1 kHz with a 16-tap windowed sinc), up to 30 s after resampling (longer
  files are cut, `smp_status` says so). Stored as int16 in the instance's `smp` array.
- **Loader thread** (modelled on Crest's `peak_bank.c`): one per process, started from the first
  `create_instance` that needs it, `SCHED_OTHER`, kept off core 3, polling every 50 ms. It reads in
  64 KB chunks into static staging (no malloc). Handshake per slot:
  1. `set_param("smp_file")` copies the path and bumps `req_gen`. The audio thread fades out every
     grain reading the sample (64 samples) and clears `smp_ready`, then sets `ack_gen = req_gen`.
  2. The worker sees `ack_gen == req_gen`, decodes into `smp`, and publishes `ready_gen` with a
     release store. A newer request cancels it between chunks.
  3. The audio thread sees `ready_gen == req_gen` and starts reading.
  Grains in Sample mode are silent while `smp_ready` is 0.
- **State** stores the path, not the audio. A Set reload requests the file again; a missing file
  leaves the source silent and `smp_status` at `error:missing`.
- **Browser build:** the page decodes with `decodeAudioData`, resamples in JS and writes the
  samples into the wasm's `smp` through an exported function; files are kept in IndexedDB by name
  so a reload finds them.
- **Live and Scan do not apply** to samples: the buffer is the file. `gr_scan` plays through it.

## 6. Factory samples
Ten short mono files in `samples/factory/` (16-bit, 44.1 kHz, 4.5–5.4 s), made by
`tools/samples/make_samples.py`, which also writes `index.json` (name, root note, length, what
each is made of, source):

| Sample | Root | Made of |
|---|---|---|
| Strings Chord | C4 | Cello, viola and violin sections on C major |
| Violins E5, Cellos C4 | E5, C4 | One section, sustained with vibrato |
| Tremolo Strings | A4 | Violin section tremolo on A4 and E5 |
| Pizzicato Walk | G3 | Violin pizzicato, G3 up to D6, a note every 0.42 s: Position picks the note |
| Harp Glissando | C3 | Eleven harp notes 0.11 s apart, ringing |
| Orchestra Tour | C4 | Cellos, violas, violins, horn and flute, all on C, 0.85 s each: Position picks the instrument |
| Choir Ah, Choir Oo, Choir Vowels | C3, C4, C3 | Synthesised (below); Vowels moves a-e-i-o-u across the file |

- **Recordings** come from VS Chamber Orchestra: Community Edition (VSCO 2 CE, Versilian Studios,
  CC0 1.0), fetched from a pinned commit into `tools/samples/cache/` (git-ignored). The script
  measures every note's pitch and tunes it (VSCO names strings, horn and flute with middle C as
  "C3"; the harp uses the usual C4), trims, mixes and fades.
- **The choir is synthesised** by the script: additive voices whose harmonics follow five vowel
  formants (published formant tables), with vibrato, detune, drift and breath noise, four singers per
  part and a short diffuse room. It is Drizzle's own material. No recorded CC0 choir was reachable when the
  set was made; a recorded one can replace it under the same names.
- **A patch names one** with `"sample": "Choir Ah"` in its JSON; `gen.py` checks the file exists and
  that the patch is in Sample mode, and writes `"factory:Choir Ah.wav"` into `DRZ_FACTORY_SAMPLES`.
  Loading the patch requests that path; the blob stores it like any other.
- **On the Move** `factory:<name>` resolves to `<module_dir>/samples/factory/<name>`, which the
  package carries (`create_instance`'s `module_dir`, kept by `smp_set_dir`).
- **In the browser** the page fetches `assets/samples/<lower_case_name>.mp3` (96 kb/s, written by
  the same script), decodes it once and hands it over as for a picked file; the single file embeds
  the MP3s (about 0.6 MB).
