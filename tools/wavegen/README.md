# Wavetables

`amino_wavetables.bin` and `manifest.json` are the output of Amino's `tools/wavegen`
(<https://github.com/klaine/amino>, MIT, same author), copied unchanged: 74 tables × 64 waves × 128
signed 8-bit samples. Waves 61–63 are a triangle, a square and a saw in every table.

`tools/gen/gen.py` turns the binary into `src/dsp/wavetables.c` and the table names into
`TABLE_NAMES` for the UI. To change the tables, regenerate them in Amino and copy both files here.

Licences are per table in `manifest.json`: 52 are Amino's own procedural tables (MIT), 22 are built
from AKWF-FREE waveforms by Kristoffer Ekstrand (CC0 1.0). See `THIRD_PARTY_NOTICES.md` and Amino's
`docs/07-wavetables-and-ip.md` for the rules.
