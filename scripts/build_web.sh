#!/bin/sh
# Build the browser synth (plan/02 section 3):
#
#   scripts/build_web.sh            # drizzle.wasm, node test, build/site, build/drizzle.html
#   scripts/build_web.sh --no-test  # skip the node test
#
# Compiles src/dsp to WebAssembly (one instance, -DDRZ_WASM) and lays out build/site: the Remote UI
# (src/web_ui.html + src/web/) driving the engine in an AudioWorklet, played from the page, the
# computer keyboard or Web MIDI.
# Serve it with: python3 -m http.server -d build/site 8765
# build/drizzle.html is the same synth in one file (scripts/build_single.py): open it from disk, or
# publish it on the pages branch with scripts/pages.sh.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

TEST=1
for arg in "$@"; do
  case "$arg" in
    --no-test) TEST=0 ;;
    -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

PY=${PYTHON:-python3}
if [ -z "${ZIG:-}" ]; then
  ZIG=$(command -v zig || "$PY" -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))' 2>/dev/null || true)
fi
if [ -z "$ZIG" ]; then echo "zig not found: brew install zig or pip install ziglang (or set ZIG=...)" >&2; exit 1; fi

mkdir -p build
"$PY" tools/gen/gen.py >/dev/null   # build/ui/*.json

echo "== drizzle.wasm ($("$ZIG" version))"
# DRZ_WASM: one instance, and no device-only 64-bit ABI assert in plugin_api.h.
"$ZIG" cc -target wasm32-wasi -mexec-model=reactor -DDRZ_WASM -std=c11 -O2 -ffast-math -fno-finite-math-only \
  -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter \
  -ffile-prefix-map="$ROOT"=. -I src/dsp -o build/drizzle.wasm src/dsp/*.c web/wasm_host.c -lm \
  -Wl,--no-entry -Wl,-z,stack-size=1048576 -Wl,--strip-all \
  -Wl,--initial-memory=33554432 -Wl,--max-memory=67108864 -Wl,--export-dynamic

if [ "$TEST" = 1 ]; then
  if command -v node >/dev/null 2>&1; then
    echo "== web test"
    node tests/web/test_wasm.mjs build/drizzle.wasm
  else
    echo "== web test: skipped (node missing)"
  fi
fi

echo "== site"
SITE=build/site
rm -rf "$SITE"
mkdir -p "$SITE/assets/ui" "$SITE/assets/engine"
cp src/web/*.js src/web/*.css build/ui/ui_meta.json "$SITE/assets/ui/"
cp web/engine.js web/worklet.js build/drizzle.wasm "$SITE/assets/engine/"
mkdir -p "$SITE/assets/samples"
cp samples/factory/*.mp3 "$SITE/assets/samples/"   # the factory samples, as the page fetches them
# The page is web_ui.html without the device bridge, so it boots the browser engine.
"$PY" - src/web_ui.html "$SITE/index.html" <<'PYEOF'
import sys
src = open(sys.argv[1], encoding="utf-8").read()
tag = '<script src="/static/schwung-remote-api.js"></script>\n'
assert tag in src, "web_ui.html no longer loads /static/schwung-remote-api.js"
open(sys.argv[2], "w", encoding="utf-8").write(src.replace(tag, ""))
PYEOF
touch "$SITE/.nojekyll"
echo "built $SITE ($(du -sh "$SITE" | cut -f1))"

"$PY" scripts/build_single.py --out build/drizzle.html
