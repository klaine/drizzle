# Third-party notices

Drizzle is MIT-licensed (see [LICENSE](LICENSE)). It includes or derives from the
following third-party material.

## Schwung (MIT)

`src/dsp/plugin_api.h` (the Schwung plugin API) and `src/dsp/move_info.h` (the
Move set-info reader) are copied from
[Schwung](https://github.com/charlesvestal/schwung), the host framework Drizzle
runs in.

```
MIT License

Copyright (c) 2025-2026 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Crest and Amino (MIT, same author)

Drizzle's build scripts, code generator, polyBLEP oscillators, envelope,
effects chain (`src/dsp/fx.c`), DSP helpers, contract validator and browser
host (`web/wasm_host.c`, `web/worklet.js`, `web/engine.js`) are adapted from
[Crest](https://github.com/klaine/crest) and
[Amino](https://github.com/klaine/amino), released under the same MIT licence
as Drizzle. From M2, Drizzle also uses Amino's wavetables; their AKWF-FREE
(CC0) sources will be credited here when they arrive.
