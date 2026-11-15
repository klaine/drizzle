# 09 — Effects

Crest's chain, adapted from Amino's `src/dsp/fx.c`, with the keys renamed only where Drizzle needs
it. A fix in one of the three modules' effects may apply to the others.

```
Σ voices → Crush → Chorus → Delay → Reverb → Volume → soft clip
```
One fixed order: granular patches want the reverb last, and Crest's six orders cost page space
for little gain here. Each effect has a level; at level 0 it switches off after 2 s (Crest).

| Effect | Keys | Notes |
|---|---|---|
| **Crush** | `cr_lvl`, `cr_type` (Soft / Hard / Fold / Bits), `cr_tone`, `cr_mix` | Crest's distortion plus **Bits**: bit depth 16→4 and sample-rate reduction from the level, for 8-bit glitch |
| **Chorus** | `cho_lvl`, `cho_type`, `cho_rate`, `cho_depth`, `cho_dly`, `cho_fb` | As Crest |
| **Delay** | `dly_lvl`, `dly_time`, `dly_sync`, `dly_fb`, `dly_mode` (Stereo / Ping-Pong / Mono), `dly_lp`, `dly_hp` | As Crest; 2 s maximum |
| **Reverb** | `rev_lvl`, `rev_type` (Room / Hall / Plate / **Cloud**), `rev_size`, `rev_decay`, `rev_damp`, `rev_hp`, `rev_pre`, `rev_frz` | **Cloud**: Hall with doubled modulation depth and diffusion, decay up to 60 s. **Freeze** holds the tank (feedback 1, input muted, damping off), the item Crest left undone |

Reverb freeze and buffer freeze are independent; the Perform page has both, and a factory
patch uses Macro 4 to fade the cloud into a frozen reverb.

State-only (`cp=False`, as in Crest): chorus and reverb EQ, reverb modulation depth and rate,
delay width and slew.

## Tests
Crest's FX suite: delay time against the sync table, RT60 per type, stability at maximum feedback,
bypass after 2 s, incremental clears. New: reverb freeze holds energy within ±1 dB over 30 s and
releases without a click; Bits at 4 bits has the expected 16 quantisation levels.
