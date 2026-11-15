# 01 — Platform & ABI

Taken from Crest's `plan/01` and Amino's `docs/02`, which are built on Schwung's `MODULES.md`.
Copy the vendored Schwung references into `docs/_schwung_*.md` (git-ignored) at M0.

## 1. Hardware
| Resource | Detail | Drizzle relevance |
|---|---|---|
| CPU | Quad Cortex-A72 @ 1.5 GHz, 2 GB RAM; audio on **core 3** | NEON available; budget in [03 §7](03-dsp-architecture.md); the sample loader runs on another core |
| Display | 128×64, 1-bit | 128 columns = one buffer overview bin each; home page draws buffer + grains |
| Encoders | 8 relative (CC 71–78), touch notes 0–7 | Host-owned page grid |
| Pads | 32, velocity + **poly aftertouch** | The chain slot receives the track's notes, not pad IDs |
| Audio | 44 100 Hz, 128 frames/block (2.902 ms), int16 stereo interleaved | |

## 2. Plugin API v2
`src/dsp/plugin_api.h` and `src/dsp/move_info.h` are vendored **verbatim** from Crest (which took
them from Schwung; MIT, credited in `THIRD_PARTY_NOTICES.md`).

```c
typedef struct plugin_api_v2 {
    uint32_t api_version;                         /* = 2 */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int   (*get_error)(void *instance, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;
```

Host services used: `get_bpm()`, `get_clock_status()`, `get_beat_position()` (may be NULL and
returns < 0 when stopped: always guard it; Tempo-mode grains and the Bar random mode need it),
and `sample_rate`. `host->log` is never called from an entry point.

### 2.1 Entry points
| Function | Drizzle behaviour |
|---|---|
| `move_plugin_init_v2` | Store `host`; start the loader thread lazily (M5, [04 §5](04-sources.md)). Return a static API struct. |
| `create_instance` | Claim a slot of `g_pool[DRZ_MAX_INSTANCES = 4]` with an atomic CAS. Load INIT, apply `json_defaults`, first `move_info_read()`. Mark the tape for rendering. No malloc. NULL when full. |
| `destroy_instance` | Cancel any pending sample request for the slot, release it (atomic store). |
| `on_midi` | Apply notes and controllers at once (Crest's deviation: same thread, no timestamps). |
| `set_param` | Generated hash lookup, store, dirty bit. Source parameters also set `tape_dirty` ([05 §4](05-source-buffer.md)). `state` parses the blob. |
| `get_param` | Static strings for contract keys; formatted values; `buf_view` / `grain_view` for the display. |
| `render_block` | FTZ on; dirty params; tape chunk; source (Live); 8 sub-blocks of 16 samples: control → grains → voices; FX; master; FTZ off. |

### 2.2 Special `get_param` keys
| Key | Returns |
|---|---|
| `chain_params` | Static generated JSON, **< 31 KiB** (host buffer 32 KiB) |
| `ui_hierarchy` | Static bare `{"levels":{…}}` |
| `state` | Full patch blob ([12](12-presets-and-state.md)) |
| `preset`, `preset_count`, `preset_name`, `pb_cats`, `pb_cat`, `pb_index`, `pb_count` | Factory browser, as in Crest |
| `mm_items` | Matrix list page |
| `buf_view` | 128 peak bins of the readable buffer as 128 hex pairs, plus the write head and tape-state flag ([11 §4](11-ui-pages.md)) |
| `grain_view` | Up to 32 live grains as `pos:amp` byte pairs (hex) |
| `<key>:effective` | Modulated value for `live:true` keys (`gr_pos`, `gr_size`, `gr_dens`, `flt_cut`) |
| `smp_status` | M5: `none`, `loading`, `ready:<frames>`, `error:<text>` |
| `cpu_us` | `--profile` builds only |

## 3. Realtime rules (non-negotiable)
Every entry point runs on the SPI audio callback (SCHED_FIFO 70, core 3), including
`create_instance`, `set_param` and `get_param`. About 2.37 ms of slack per block is shared with Move
and every other module.

Forbidden in every entry point: file I/O, `malloc`/`free`, mutexes and blocking syscalls, any
logging, `dlopen`/`fork`, unbounded loops, memsets over 64 KB in one block.

Consequences for Drizzle:
- **Static buffers.** Source buffers, tape back buffer, sample storage, grain pool, delay and
  reverb lines live in the instance struct in `.bss` ([03 §3](03-dsp-architecture.md)).
- **Bounded tape rendering.** The Scan tape is rendered in fixed chunks per block, never in one go
  ([05 §4](05-source-buffer.md)).
- **Lazy clears.** Buffers are cleared or rewritten incrementally; grains are muted while a buffer
  is not yet valid.
- **Files only on the worker.** WAV parsing, resampling and normalising happen on the loader
  thread (M5). The audio thread copies a path in and a sequence number out.

## 4. `module.json`
```json
{
  "id": "drizzle",
  "name": "Drizzle",
  "abbrev": "DRZL",
  "version": "0.1.0",
  "api_version": 2,
  "description": "Granular synth: a wavetable or analog source re-read as grain clouds",
  "author": "AI & Klaine",
  "dsp": "dsp.so",
  "component_type": "sound_generator",
  "capabilities": {
    "chainable": true,
    "midi_in": true,
    "audio_out": true,
    "aftertouch": true,
    "default_forward_channel": -2
  }
}
```
Rules (from Crest): no comments, under 8 KB, no `ui_hierarchy` here, the DSP file must be
`dsp.so`, `claims_edit_ccs` stays false.

## 5. Install layout on device
```
/data/UserData/schwung/modules/sound_generators/drizzle/
  dsp.so  module.json  help.json  LICENSE  web_ui.html  canvas.js  cards.js  ui/…
/data/UserData/UserLibrary/Drizzle/        user samples (M5), .wav, subfolders allowed
```

## 6. MIDI input
| Message | Handling |
|---|---|
| 0x90 / 0x80 | Note on / off ([07](07-voices-filter-amp.md)); release velocity stored |
| 0xA0 poly AT | Per-voice pressure (pressure page and matrix) |
| 0xD0 channel AT | Global pressure; also applied to voices without poly AT |
| 0xE0 bend | Global, range `v_bend` |
| CC 1 / 2 / 11 | Wheel / Breath / Expression |
| CC 64 | Sustain |
| CC 69 | Freeze: ≥ 64 toggles `buf_freeze` on a rising edge (**VERIFY** that the chain slot passes it) |
| CC 120 / 121 / 123 | All sound off / reset controllers / all notes off |

`source`: accept `INTERNAL`, `EXTERNAL` and `HOST`; ignore `TOUCH`.
