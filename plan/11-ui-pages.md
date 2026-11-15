# 11 — Move UI Pages

## 1. Approach
- **Host knob grid** from `get_param("ui_hierarchy")`, plus Crest's module-drawn additions:
  a **home page** canvas in front of the root knobs, the **Patches** canvas browser, and
  **cards** (`src/cards.js`) that show units for knobs whose raw value means little (sizes in ms or
  steps, density in grains/s, buffer length in s, divisions, note names, matrix routes).
- **Page rules** (Amino's `docs/05`, as used by Crest): 2×4 grid; top row = what, bottom row =
  how much or how it moves; the same position does the same job on sibling pages (Sweep and
  Source LFO share their bottom row: Timbre, Fold, Pitch, Tone); viz groups contiguous within a
  row; short names ≤ 5, short options ≤ 4; selectors never in `knobs`.
- **No `visible_if`.** Wavetable and Analog pages are always reachable; the inactive type's knobs
  keep their values and do nothing. Help says so.

## 2. Page order (the jog walk)
```
Home (canvas) ─► Main (root) ─► Patches ─► Perform
 ─► Source ─► Wavetable ─► Wavetable Mod ─► Analog ─► Analog Mod ─► Sample (M5) ─► Sweep ─► Source LFO
 ─► Grains ─► Grain Pitch ─► Rhythm
 ─► Filter ─► Amp Env ─► Mod Env ─► Env Options ─► Voice
 ─► LFO 1 ─► LFO 2 ─► Pressure ─► Mod Matrix (list) → Matrix Edit
 ─► Crush ─► Chorus ─► Delay ─► Reverb
 ─► (host) My Presets ─► Module (+ Help)
```
Shift+jog-click sections: Main, Patches, Perform, Source, Grains, Voice, Modulation, FX (Crest's
nested-level pattern).

## 3. Page layouts
| Level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| **Main** (root) | Pos | Spray | Size | Dens | PSpr | Scan | Space (`rev_lvl`) | Frz |
| Perform | M1 | M2 | M3 | M4 | Frz | RFrz | Flav | Vol |
| Source | Src | Mode (`buf_mode`) | Root | Track | Len | Fold | Tone | Seed |
| Wavetable | TblA | WavA | TblB | WavB | Semi | Fine | LvlB | Qual |
| Wavetable Mod | XMod | XAmt | – | – | – | – | – | – |
| Analog | WavA | ShpA | WavB | ShpB | Semi | Fine | LvlB | Noise |
| Analog Mod | Dens | DDet | FM | Sync | – | – | – | – |
| Sample (M5) | File | Root | Start | End | Norm | – | – | – |
| Sweep | Time | Curve | Mode | – | Timb | Fold | Pitch | Tone |
| Source LFO | Wave | Rate | Sync | – | Timb | Fold | Pitch | Tone |
| Grains | Pos | Spray | Size | Dens | Jit | Scan | Win | Lock |
| Grain Pitch | Pitch | Fine | PSpr | PQnt | Rev | Sprd | LvSpr | – |
| Rhythm | Mode | Div | Swing | Prob | Rpt | Rand | Seed | – |
| Filter | Type | Cut | Res | Drive | Env | KeyTk | Vel | – |
| Amp Env | Atk | Dec | Sus | Rel | – | – | – | – |
| Mod Env | Atk | Dec | Sus | Rel | Pos | Size | Dens | Pitch |
| Env Options | AVel | MVel | MTrig | RThin | – | – | – | – |
| Voice | Mode | Voics | Glide | Bend | Oct | Flav | Pan | – |
| LFO 1 | Wave | Rate | Sync | Trig | Pos | Size | Pitch | Cut |
| LFO 2 | Wave | Rate | Sync | Phase | Pos | Dens | Spray | Pan |
| Pressure | Pos | Dens | Spray | Size | PSpr | Cut | – | – |
| Matrix Edit | Src | Via | Dest | Depth | Slot | Clear | – | – |
| Crush | Level | Type | Tone | Mix | – | – | – | – |
| Chorus | Level | Type | Rate | Depth | Dly | Fb | – | – |
| Delay | Level | Time | Sync | Fb | Mode | LP | HP | – |
| Reverb | Level | Type | Size | Decay | Damp | HP | Pre | RFrz |

The Main page repeats the Grains page's top row on purpose: the four knobs a granular patch lives
on are the first thing under the hands.

## 4. Home page (canvas)
128×64, 1-bit, drawn by `src/canvas.js` (Crest's home page mechanism, `page_first`):
```
┌────────────────────────────────────────┐
│ Glass Rain               SCAN  4.0s  ❄ │  name, mode, length, freeze
│▁▂▃▅▆▇▇▆▅▃▂▁▁▂▃▅▇▇▆▅▄▃▂▂▃▄▅▆▇▆▅▄▃▂▁▁▂▃▄▅│  128 peak bins (mirrored waveform)
│     [·· ·  ·:· ·  ··]                  │  position ± spray window
│  •    •  ••  •   •                     │  live grains: x = position, y = level
│ Pos 0.42  Spray 0.18  Dens 24/s        │
└────────────────────────────────────────┘
```
- Data from `buf_view` and `grain_view`, polled by the canvas. `buf_view` is hex: mode (`L`, `S`, or `M`
  for Sample), frozen (`0`/`1`), tape-render progress (two digits, `FF` = none), head position (two
  digits), then 128 bins of peak level and 128 of brightness (two digits each, oldest first in Live):
  518 characters. The Move draws brightness as the fill's dither density (dark timbres sparse,
  bright ones solid, the level's outline always drawn); the web UI as the bars' colour, from
  `--dark-tone` to `--bright-tone`. `wave_view` (web UI only: the home page has no room for a fifth
  extra key) is 64 signed-byte points of about two periods of the material at the newest voice's
  Position, drawn as a zoom beside the buffer. `grain_view` is
  four hex digits per live grain (up to 32): position along the readable range and the window's
  current level. **VERIFY** the poll rate and that ~300-byte answers are cheap enough at that rate; fall
  back to bins every 4th frame.
- In Live the picture scrolls with the head; frozen, it stops and shows the snowflake.
- During a tape render a progress line runs under the header.
- `extra_keys`: `buf_mode`, `buf_freeze`, `preset_name`, `gr_pos`, `gr_spray`, `gr_dens`.

## 5. Matrix list page
Crest's pattern: `"matrix": {"items_param":"mm_items","select_param":"mm_sel",
"navigate_to":"matrix_edit"}`. `mm_items` returns 12 entries such as `{"index":2,"label":"3: LFO2xWhl>Pos"}`;
an empty slot reads `5: –`.

## 6. Patches
The shared preset browser (Crest's `browse` canvas, `pb_cats` / `pb_cat` / `pb_index`): categories
from `CATEGORIES` in `params.py` ([12 §4](12-presets-and-state.md)).

## 7. Help (`help.json`)
Lines ≤ 20 ASCII characters, top level with `children`. Topics: Overview, Source, Wavetable,
Analog, Buffer & Freeze, Scan Tapes, Sweep & S.LFO, Grains, Pitch & Lock, Rhythm, Filter & Env,
Modulation, Pressure, FX, Tips (e.g. "Jitter 0 + high density = a pitched buzz", "Lock 1 = smooth
tone").

## 8. Remote UI and web
`src/web_ui.html` + `src/web/` as in Crest: panel layout of every section, the full 12-slot
matrix grid, the shared preset browser (`presetbrowser.js`, `presetdrawer.js`, `presetbrowser.css`
are byte-identical copies of Amino's canonical files), and a large buffer view with live grains
drawn from `buf_view` / `grain_view`. The same UI drives `drizzle.wasm` in `build/site` and in the
single file `build/drizzle.html`. In the browser, Sample mode takes a dropped or picked `.wav`.

## 9. Contract generation
`gen.py` builds `chain_params` and `ui_hierarchy` from `params.py` (`LEVELS`, as in Crest), strips
descriptions, writes `contract_gen.c` and `build/ui/*.json`, and prints the minified sizes.
