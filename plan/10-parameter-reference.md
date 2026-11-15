# 10 — Parameter Reference

The canonical list will be `tools/gen/params.py`; this table is what it starts from. Defaults are
the INIT patch ([12 §5](12-presets-and-state.md)). Short names are ≤ 5 characters, enum short
options ≤ 4. **CP** = in `chain_params` (a knob or list on Move); keys marked *state* are saved in
patches and edited in the web UI only (`cp=False`, Crest's mechanism for staying under 31 KiB).
Float values are normalised `0..1` unless a unit is given; the curve column says how they map.

## Global & buffer
| Key | Name | Short | Type | Range | Default | Curve / notes |
|---|---|---|---|---|---|---|
| `patch_vol` | Volume | Vol | float | 0..1 | 0.5 | gain `4·x²` (0.5 = unity, 1 = +12 dB) |
| `buf_mode` | Buffer Mode | Mode | enum | Live, Scan | Scan | |
| `buf_len` | Buffer Length | Len | float | 0..1 | 0.75 (4 s) | 0.5..8 s exp |
| `buf_freeze` | Freeze | Frz | enum | Off, On | Off | CC 69 toggles |

## Source (common)
| Key | Name | Short | Type | Range | Default | Notes |
|---|---|---|---|---|---|---|
| `src_type` | Source | Src | enum | Wavetable (WT), Analog (Anlg), Sample (Smpl) | Wavetable | Sample from M5 |
| `src_root` | Source Root | Root | int | 24..96 | 48 | note |
| `src_track` | Track Notes | Track | enum | Off, On | Off | Live only |
| `src_fold` | Fold | Fold | float | 0..1 | 0 | |
| `src_tone` | Tone | Tone | float | 0..1 | 1 | 150 Hz..20 kHz exp, 1 = bypass |
| `src_seed` | Source Seed | Seed | int | 0..999 | 0 | |

## Wavetable source
| Key | Name | Short | Type | Range | Default |
|---|---|---|---|---|---|
| `wt_a_table`, `wt_b_table` | Table A / B | TblA / TblB | enum | Amino's 74 table names | 0 / 0 |
| `wt_a_wave`, `wt_b_wave` | Wave A / B | WavA / WavB | int | 0..63 | 0 / 0 |
| `wt_b_semi` | Osc B Semi | Semi | int | −24..24 | 0 |
| `wt_b_fine` | Osc B Fine | Fine | float | −50..50 ct | 0 |
| `wt_b_lvl` | Osc B Level | LvlB | float | 0..1 | 0 |
| `wt_xmod` | Cross-mod | XMod | enum | Off, Ring, PM, Sync | Off |
| `wt_xamt` | Cross-mod Amount | XAmt | float | 0..1 | 0 |
| `wt_q` | Quality | Qual | enum | Vintage (Vint), Clean (Cln) | Vintage |

## Analog source
| Key | Name | Short | Type | Range | Default |
|---|---|---|---|---|---|
| `an_a_wave`, `an_b_wave` | Wave A / B | WavA / WavB | enum | Sine (Sin), Tri, Saw, Pulse (Pls) | Saw / Pulse |
| `an_a_shape`, `an_b_shape` | Shape A / B | ShpA / ShpB | float | 0..1 | 0 / 0.5 |
| `an_b_semi` | Osc B Semi | Semi | int | −24..24 | 0 |
| `an_b_fine` | Osc B Fine | Fine | float | −50..50 ct | 7 |
| `an_b_lvl` | Osc B Level | LvlB | float | 0..1 | 0 |
| `an_dens` | Density | Dens | float | 0..1 | 0 |
| `an_ddet` | Density Detune | DDet | float | 0..1 | 0.3 |
| `an_fm` | FM B→A | FM | float | 0..1 | 0 |
| `an_sync` | Sync | Sync | float | 0..1 | 0 (off) .. ×8 |
| `an_noise` | Noise | Noise | float | 0..1 | 0 |

## Sweep and Source LFO
| Key | Name | Short | Type | Range | Default | Notes |
|---|---|---|---|---|---|---|
| `sw_time` | Sweep Time | Time | float | 0..1 | 0.5 | 0.1..60 s exp, Live only |
| `sw_curve` | Sweep Curve | Curve | float | −1..1 | 0 | |
| `sw_mode` | Sweep Mode | Mode | enum | One, Loop, Bounce (Bnce) | One | |
| `sw_timb` / `sl_timb` | Sweep / LFO → Timbre | Timb | float | −1..1 | 1 / 0 | |
| `sw_fold` / `sl_fold` | → Fold | Fold | float | −1..1 | 0 | |
| `sw_pitch` | Sweep → Pitch | Pitch | float | −24..24 st | 0 | |
| `sl_pitch` | LFO → Pitch | Pitch | float | −12..12 st | 0 | |
| `sw_tone` / `sl_tone` | → Tone | Tone | float | −1..1 | 0 | |
| `sl_wave` | Source LFO Wave | Wave | enum | Sine, Tri, Saw, Square (Sqr), S&H, Smooth (Smth) | Sine | |
| `sl_rate` | Source LFO Rate | Rate | float | 0..1 | 0.3 | 0.01..40 Hz exp |
| `sl_sync` | Source LFO Sync | Sync | enum | Off, 1/16 … 8 bars (12 entries) | Off | |

## Grains
| Key | Name | Short | Type | Range | Default | Notes |
|---|---|---|---|---|---|---|
| `gr_pos` | Position | Pos | float | 0..1 | 0.5 | live |
| `gr_spray` | Spray | Spray | float | 0..1 | 0.1 | |
| `gr_size` | Size | Size | float | 0..1 | 0.55 (≈ 135 ms) | Cloud 5 ms..2 s; Tempo 1/16..4 steps; live |
| `gr_dens` | Density | Dens | float | 0..1 | 0.5 (≈ 11/s) | 0.5..250 /s exp; live |
| `gr_jit` | Jitter | Jit | float | 0..1 | 0.5 | |
| `gr_scan` | Scan Speed | Scan | float | −4..4 | 0 | cubic |
| `gr_win` | Window | Win | enum | Hann, Gauss (Gaus), Tukey (Tuky), Perc, Swell (Swel), Rect | Hann | |
| `gr_lock` | Period Lock | Lock | float | 0..1 | 0 | |
| `gr_pitch` | Grain Pitch | Pitch | int | −24..24 | 0 | |
| `gr_fine` | Grain Fine | Fine | float | −50..50 ct | 0 | |
| `gr_pspr` | Pitch Spray | PSpr | float | 0..24 st | 0 | |
| `gr_pq` | Pitch Quantise | PQnt | enum | Off, Oct, Fifth (5th), Scale (Scl) | Oct | |
| `gr_rev` | Reverse | Rev | float | 0..1 | 0 | probability |
| `gr_spread` | Stereo Spread | Sprd | float | 0..1 | 0.5 | |
| `gr_lspr` | Level Spray | LvSpr | float | 0..1 | 0 | |
| `gr_mode` | Grain Mode | Mode | enum | Cloud (Cld), Tempo (Tmpo) | Cloud | |
| `gr_div` | Division | Div | enum | 1/64 … 1 bar (14 entries, [06 §2](06-grain-engine.md)) | 1/16 | |
| `gr_swing` | Swing | Swing | float | 0..0.75 | 0 | |
| `gr_prob` | Probability | Prob | float | 0..1 | 1 | |
| `gr_rpt` | Repeats | Rpt | int | 0..16 | 0 | |
| `gr_rnd` | Random | Rand | enum | Free, Note, Bar | Free | |
| `gr_seed` | Grain Seed | Seed | int | 0..999 | 0 | |
| `gr_relth` | Release Thinning | RThin | enum | Off, On | On | |

## Voice, filter, envelopes
| Key | Name | Short | Type | Range | Default |
|---|---|---|---|---|---|
| `v_mode` | Voice Mode | Mode | enum | Poly, Mono, Legato (Leg) | Poly |
| `v_voices` | Voices | Voics | int | 1..8 | 8 |
| `v_glide` | Glide | Glide | float | 0..1 (0..2 s exp) | 0 |
| `v_bend` | Bend Range | Bend | int | 0..24 | 2 |
| `v_oct` | Octave | Oct | int | −2..2 | 0 |
| `v_flav` | Flavour | Flav | float | 0..1 | 0.3 |
| `v_pan` | Pan | Pan | float | −1..1 | 0 |
| `flt_type` | Filter Type | Type | enum | Off, LP24, LP12, BP12, HP12 | Off |
| `flt_cut` | Cutoff | Cut | float | 0..1 | 0.8 (live) |
| `flt_res` | Resonance | Res | float | 0..1 | 0.1 |
| `flt_drive` | Filter Drive | Drive | float | 0..1 | 0 |
| `flt_env` | Filter Env | Env | float | −1..1 | 0 |
| `flt_key` | Key Track | KeyTk | float | 0..1 | 0.5 |
| `flt_vel` | Filter Velocity | Vel | float | 0..1 | 0 |
| `ae_a`, `ae_d`, `ae_s`, `ae_r` | Amp A/D/S/R | Atk Dec Sus Rel | float | 0..1 (Crest's curves) | 0.3 s / 0.5 s / 0.8 / 1.2 s |
| `ae_vel` | Amp Velocity | AVel | float | 0..1 | 0.5 |
| `me_a`, `me_d`, `me_s`, `me_r` | Mod A/D/S/R | Atk Dec Sus Rel | float | 0..1 | 2 s / 2 s / 0.5 / 2 s |
| `me_vel` | Mod Velocity | MVel | float | 0..1 | 0 |
| `me_trig` | Mod Env Trigger | MTrig | enum | Note, First (Frst) | Note |
| `me_pos`, `me_size`, `me_dens`, `me_pitch` | Mod Env → … | Pos Size Dens Pitch | float | −1..1 (pitch −24..24) | 0 |

## LFOs, pressure, macros
| Key | Name | Short | Type | Range | Default |
|---|---|---|---|---|---|
| `l1_wave`, `l2_wave` | LFO Wave | Wave | enum | Sine, Tri, Saw Up (SawU), Saw Down (SawD), Square (Sqr), S&H, Smooth (Smth) | Sine / Tri |
| `l1_rate`, `l2_rate` | LFO Rate | Rate | float | 0..1 (0.01..40 Hz exp) | 0.4 / 0.2 |
| `l1_sync`, `l2_sync` | LFO Sync | Sync | enum | Off, 1/32 … 16 bars (20 entries) | Off |
| `l1_trig` | LFO 1 Trigger | Trig | enum | Free, Note, Note+Random (NRnd) | Note |
| `l2_phase` | LFO 2 Phase | Phase | float | 0..1 | 0 |
| `l1_pos`, `l1_size`, `l1_pitch`, `l1_cut` | LFO 1 → … | Pos Size Pitch Cut | float | −1..1 (pitch ±12) | 0 |
| `l2_pos`, `l2_dens`, `l2_spray`, `l2_pan` | LFO 2 → … | Pos Dens Spray Pan | float | −1..1 | 0 |
| `at_pos`, `at_dens`, `at_spray`, `at_size`, `at_pitch`, `at_cut` | Pressure → … | Pos Dens Spray Size PSpr Cut | float | −1..1 | 0, 0.3, 0.2, 0, 0, 0 |
| `mac1` … `mac4` | Macro 1–4 | M1 … M4 | float | 0..1 | 0 |

## Matrix
| Key | Type | Notes |
|---|---|---|
| `mm01_src` … `mm12_src` | enum (24 sources, [08 §4](08-modulation.md)) | state |
| `mm01_via` … `mm12_via` | enum (same list) | state |
| `mm01_dst` … `mm12_dst` | enum (32 destinations) | state |
| `mm01_dep` … `mm12_dep` | float −1..1 | state |
| `mm_sel` | selector (list page) | not in state, not a knob |
| `mm_src`, `mm_via`, `mm_dst`, `mm_dep`, `mm_slot`, `mm_clear` | proxies of the selected slot | CP, not in state |

## Effects
| Key | Short | Type | Range | Default |
|---|---|---|---|---|
| `cr_lvl` | Level | float | 0..1 | 0 |
| `cr_type` | Type | enum | Soft, Hard, Fold, Bits | Soft |
| `cr_tone`, `cr_mix` | Tone, Mix | float | 0..1 | 0.7, 1 |
| `cho_lvl`, `cho_type`, `cho_rate`, `cho_depth`, `cho_dly`, `cho_fb` | as Crest | | | 0, Chorus 1, 0.3, 0.5, 0.3, 0 |
| `dly_lvl`, `dly_time`, `dly_sync`, `dly_fb`, `dly_mode`, `dly_lp`, `dly_hp` | as Crest | | | 0, 0.5, 1/8D, 0.35, Ping-Pong, 0.6, 0.1 |
| `rev_lvl` | Level (Main: Space) | float | 0..1 | 0.3 |
| `rev_type` | Type | enum | Room, Hall, Plate, Cloud (Cld) | Hall |
| `rev_size`, `rev_decay`, `rev_damp`, `rev_hp`, `rev_pre` | as Crest | | | 0.7, 0.6, 0.4, 0.15, 0.04 |
| `rev_frz` | RFrz | enum | Off, On | Off |
| `cho_lp`, `cho_hp`, `dly_width`, `dly_slew`, `rev_lpf`, `rev_hpf`, `rev_mdep`, `rev_mrate` | | float | | state (Crest's defaults) |

## Sample (M5)
| Key | Short | Type | Range | Default |
|---|---|---|---|---|
| `smp_file` | File | filepath | root `/data/UserData/UserLibrary/Drizzle`, filter `.wav` | "" |
| `smp_root` | Root | int | 0..127 | 60 |
| `smp_start`, `smp_end` | Start, End | float | 0..1 | 0, 1 |
| `smp_norm` | Norm | enum | Off, On | On |

## Browser and special keys
`preset`, `pb_cat`, `pb_index` and the read-only keys in [01 §2.2](01-platform-and-abi.md), as in
Crest.

About 165 keys reach `chain_params`; with the two 74-entry table enums it should come to roughly
16–18 KB, about half the limit. `gen.py` prints the size, and the validator fails above 31 KiB.
