# Contributing to Drizzle

Bug reports, patch ideas and pull requests are welcome. For anything bigger than a small fix, open an
issue first so we can agree on the approach.

- **Branch:** fork, branch from `main`, and open the pull request against `main`.
- **Build and test:** `scripts/build.sh` runs the native tests (twice: once on each grain read path),
  the contract validator, builds `dsp.so`, checks its symbols and packages it. It needs
  [zig](https://ziglang.org) (or `pip install ziglang`) and Python 3. Run it before you open a pull request.
- **Spec:** `plan/` is the spec, and once code exists the code is the source of truth. If they
  disagree, fix one of them in the same pull request, and update `plan/16-implementation-status.md`
  when what's built changes.
- **Realtime:** no allocation, file I/O or logging on the audio thread; bounded work per block.
- **Third-party material:** contributed patches, wavetables and samples must be your own work or
  CC0, and credited in `THIRD_PARTY_NOTICES.md`.
- **Style:** match the surrounding code, keep `-Wall -Wextra -Werror` clean, and keep commits focused.

By contributing you agree your work is released under the repository's [LICENSE](LICENSE).
