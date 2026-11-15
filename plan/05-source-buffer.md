# 05 — Source Buffer

The buffer is what every grain reads. `buf_mode` decides how the source fills it.

| Mode | The buffer holds | Position means | Use |
|---|---|---|---|
| **Live** | The last `buf_len` seconds of the source, written continuously | How far back from the write head (0 = newest) | Moving, breathing clouds; `src_track` re-renders at the played pitch |
| **Scan** | A **tape**: the source rendered once, `buf_len` long, with Sweep and Source LFO in tape time | Where along the tape (0 = start of the sweep, 1 = end) | Timbre scanning, drones, stable patches that sound the same on every load |
| Sample (M5) | The file region `smp_start..smp_end` | Where in the region | Classic sample granular |

`buf_len` is 0.5..8 s (exponential, default 4 s). `buf_freeze` (Off/On) stops writing in Live; in
Scan it suspends re-renders, so source knob changes no longer reach the tape until it is released.

## 1. Memory
Two float buffers `data[0..1]` of `DRZ_BUF_MAX` (8 s) per instance. Live uses `data[0]` as a ring.
Scan uses them as front and back: grains read the front while the back renders. Sample mode reads
`smp`.

## 2. Live mode
- **Write.** `source_live` renders 128 samples per block into the ring at `head`. The ring always
  wraps at `DRZ_RING` (8 s); `buf_len` only limits how far back grains read, so changing it never
  scrambles what is recorded. The head is an absolute sample count (int64); a slot is `abs % DRZ_RING`.
- **Read guard.** Positions are aligned to the output: output sample `i` of a block is absolute
  `head_after_write − 128 + i`, called *now*. A grain starting `d` samples behind now with step `inc`
  and length `n` may not overtake the newest written sample, nor fall behind the oldest one still in
  the ring, while it plays. At onset Drizzle clamps
  `4 + n · max(inc − 1, 0) ≤ d ≤ L − 136 − n · max(1 − inc, 0)` with `L = min(buf_len, valid)`
  (the margins cover the 4-point read and the block written ahead). When the bounds cross (a long
  grain pitched far off in a short buffer), the grain is shortened to `(L − 140) / |inc − 1|`, or
  skipped below 64 samples. Test builds tag every slot with its absolute position and check every
  point every grain reads ([13 §2](13-testing.md)).
- **Filling.** After load, a buffer change or a source-type change, the ring holds less than
  `buf_len` of valid audio; `valid` counts it and the read guard uses `valid` instead of `buf_len`.
  Grains with no valid audio to read are not spawned.
- **Changing `buf_len`** in Live takes effect at once for new grains; the ring keeps its contents.
- **Reset** (a state load) sets `valid` to 0 but keeps the head's absolute position, so grains still
  fading out read what they meant to while new writes land ahead of them.

## 3. Freeze
- **Live → frozen.** The loop is the newest `min(buf_len, valid)` samples. The head stops at once;
  the source runs on for 256 more samples, which are crossfaded (equal power) into the first 256
  samples of the loop, so its end runs on into its start without a seam. Playing Live grains are
  converted to loop coordinates, so one that was reading towards the head continues into the loop's
  start instead of into stale data. Position is then measured back from the frozen head, and grains
  wrap round the loop freely.
- **Unfreeze** resumes writing at the head. Grains that are playing keep their loop positions; the
  read guard applies again to new grains. The first new sample does not continue the frozen loop's
  last one, so a grain crossing the old head after an unfreeze can click: a listening item for M1.
- **CC 69** toggles freeze ([01 §6](01-platform-and-abi.md)); the Main page has a knob for it.
- **State.** A frozen Live buffer cannot be saved, and the blob would be far too large.
  On load with `buf_freeze` On in Live mode, the ring starts empty and the freeze waits until the
  source has recorded `buf_len` again, then freezes that (`wait_full`). There is no offline render,
  so no CPU spike on load. The sound is close to what was frozen, not identical; [12 §2](12-presets-and-state.md)
  notes this for users. A freeze asked for while less than 1 024 samples are recorded waits the same way.

## 4. Scan mode: the tape
- **Rendering.** The source renders `buf_len` seconds at `src_root` into the back buffer,
  `DRZ_TAPE_CHUNK` (1 024) samples per block, which is 8× realtime: a 4 s tape takes 0.5 s.
  Sweep runs from 0 to 1 across the tape, and Source LFO in tape time from phase 0.
- **Determinism.** The tape depends only on the patch (base values, `src_seed`) and, for synced
  Source LFO, the tempo when it was rendered. Golden tests rely on it.
- **Debounce.** Any change to a source key sets `tape_dirty` and restarts a 60 ms timer. When it
  expires, the render starts; a further change during a render restarts it from the beginning, so
  a knob turned continuously renders at most every 60 ms + render time.
- **Swap.** When the back buffer is complete, front and back swap. Grains already playing keep
  reading the buffer they started in (`grain.buf`), so the old front must stay intact until they end:
  the next render waits until no grain reads the old front (at most the longest grain, 2 s).
  New grains read the new front at once. The swap is click-free because no grain changes buffer
  while it plays.
- **First tape.** On load, nothing is audible until the first tape is ready (≤ 1 s for 8 s). Notes
  held meanwhile start their grains when it arrives; the amp envelope already runs, so the cloud
  fades in. Factory patches use 2–6 s tapes.
- **Ends.** Position is clamped to the tape. Spray reflects off the ends rather than wrapping,
  because the two ends of a sweep are unrelated timbres.

## 5. Display bins
For the home page ([11 §4](11-ui-pages.md)) the buffer keeps 128 peak bins of the readable range:
- Scan and Sample: computed once per tape or load, during the render (or by the worker).
- Live: one bin is refreshed per block, round-robin. A bin spans `buf_len / 128` samples (at most
  2 756, a few µs of `fabsf`), so the whole picture follows the ring in 128 blocks, about 0.37 s.
`buf_view` formats them with the head position, freeze and render-progress flags.
