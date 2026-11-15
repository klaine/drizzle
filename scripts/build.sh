#!/bin/sh
# Build Drizzle with zig cc: contract validation, native tests, the aarch64 dsp.so, its symbol
# check and drizzle-module.tar.gz (plan/02 section 2). Run from anywhere:
#
#   scripts/build.sh            # gen --check, contract, native tests, device build, package
#   scripts/build.sh --gen      # run tools/gen/gen.py first (after editing params.py or presets)
#   scripts/build.sh --no-test  # skip the tests
#   scripts/build.sh --profile  # dsp.so with the read-only cpu_us key (render time, not for release)
#   scripts/build.sh --bench    # also build and run the native CPU benchmark
#   scripts/build.sh --render   # also write demo WAVs to build/demo
#
# Needs zig (brew install zig, or pip install ziglang) and python3.
# Set ZIG=/path/to/zig to override the lookup.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

GEN=0
TEST=1
PROFILE=0
BENCH=0
RENDER=0
for arg in "$@"; do
  case "$arg" in
    --gen) GEN=1 ;;
    --no-test) TEST=0 ;;
    --profile) PROFILE=1 ;;
    --bench) BENCH=1 ;;
    --render) RENDER=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

PY=${PYTHON:-python3}
BUILD="$ROOT/build"
MODULE="$ROOT/modules/sound_generators/drizzle"
mkdir -p "$BUILD" "$MODULE"

find_zig() {
  if [ -n "${ZIG:-}" ]; then echo "$ZIG"; return; fi
  if command -v zig >/dev/null 2>&1; then command -v zig; return; fi
  "$PY" -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))' 2>/dev/null || true
}
ZIG=$(find_zig)
if [ -z "$ZIG" ] || [ ! -x "$ZIG" ]; then
  echo "zig not found: brew install zig or pip install ziglang (or set ZIG=...)" >&2
  exit 1
fi
echo "== zig $("$ZIG" version)"

# Same math flags on the device and in the tests: -ffast-math without assuming finite values, so
# the NaN guard in render_block keeps working.
WARN="-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter"
MATH="-ffast-math -fno-finite-math-only"
DEFS=""
[ "$PROFILE" = 1 ] && DEFS="-DDRZ_PROFILE"
DSP_SRC=$(ls src/dsp/*.c)

if [ "$GEN" = 1 ]; then
  echo "== gen"
  "$PY" tools/gen/gen.py
else
  "$PY" tools/gen/gen.py --check
  "$PY" tools/gen/gen.py >/dev/null   # build/ is not committed: the contract and UI data for the checks
fi

if [ "$TEST" = 1 ]; then
  echo "== native tests"
  SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
  # -DDRZ_TEST tags every ring sample so the read guard is checked on every sample a grain reads.
  "$ZIG" cc -std=c11 -O1 -g $SAN $WARN $MATH -DDRZ_TEST -I src/dsp \
    -o "$BUILD/drizzle_test" $DSP_SRC tests/host/test_main.c -lm -lpthread
  "$BUILD/drizzle_test"
  # once more with every grain on the wrapping (slow) read path, so both paths stay tested
  "$ZIG" cc -std=c11 -O1 -g $SAN $WARN $MATH -DDRZ_TEST -DDRZ_TEST_SLOW -I src/dsp \
    -o "$BUILD/drizzle_test_slow" $DSP_SRC tests/host/test_main.c -lm -lpthread
  if ! "$BUILD/drizzle_test_slow" >"$BUILD/test_slow.log"; then
    cat "$BUILD/test_slow.log"
    exit 1
  fi
  tail -n 1 "$BUILD/test_slow.log"

  echo "== contract"
  "$PY" tests/contract/validate_contract.py
  if command -v node >/dev/null 2>&1; then
    node tests/device/test_device_ui.mjs
  else
    echo "== device ui test: skipped (node missing)"
  fi

  echo "== golden"
  "$ZIG" cc -std=c11 -O2 $WARN $MATH -I src/dsp -o "$BUILD/drizzle_golden" $DSP_SRC tests/golden/golden_main.c -lm -lpthread
  "$PY" tests/golden/check_golden.py "$BUILD/drizzle_golden"
fi

if [ "$BENCH" = 1 ]; then
  echo "== bench"
  "$ZIG" cc -std=c11 -O2 $WARN $MATH -I src/dsp -o "$BUILD/drizzle_bench" $DSP_SRC tests/host/bench_main.c -lm -lpthread
  "$BUILD/drizzle_bench"
fi

if [ "$RENDER" = 1 ]; then
  echo "== demo renders"
  mkdir -p "$BUILD/demo"
  "$ZIG" cc -std=c11 -O2 $WARN $MATH -I src/dsp -o "$BUILD/drizzle_render" $DSP_SRC tests/host/render_wav.c -lm -lpthread
  "$BUILD/drizzle_render" "$BUILD/demo"
fi

echo "== dsp.so (aarch64-linux-gnu)"
# -ffile-prefix-map: no absolute build paths (and so no user name) in shipped binaries.
"$ZIG" cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a72 -std=c11 -O2 $WARN $MATH $DEFS \
  -fPIC -shared -fvisibility=hidden -s -ffile-prefix-map="$ROOT"=. -I src/dsp \
  -o "$MODULE/dsp.so" $DSP_SRC -lm -lpthread -ldl -lrt  # pthread: the sample loader; dl, rt: move_info.h
rm -f "$MODULE/dsp.so.o"

echo "== check dsp.so"
"$PY" scripts/check_so.py "$MODULE/dsp.so"

cp src/module.json src/help.json src/canvas.js src/cards.js src/web_ui.html LICENSE "$MODULE/"
# Remote UI (Schwung's browser view of the slot): the app and its generated data
rm -rf "$MODULE/assets"
mkdir -p "$MODULE/assets/ui"
cp src/web/*.js src/web/*.css build/ui/ui_meta.json "$MODULE/assets/ui/"

echo "== package"
# COPYFILE_DISABLE keeps macOS tar from adding ._ AppleDouble files; --numeric-owner leaves the
# builder's user and group names out of the headers, --no-xattrs the com.apple.* attributes.
COPYFILE_DISABLE=1 tar -czf "$ROOT/drizzle-module.tar.gz" --numeric-owner --no-xattrs \
  --exclude '.DS_Store' --exclude '.gitkeep' -C "$ROOT/modules/sound_generators" drizzle

[ "$TEST" = 1 ] && WEBARGS="" || WEBARGS="--no-test"
ZIG="$ZIG" PYTHON="$PY" sh scripts/build_web.sh $WEBARGS   # build/site and build/drizzle.html
sh scripts/pages.sh   # build/drizzle.html as index.html on the local pages branch (push: scripts/pages.sh --push)

ls -l "$MODULE/dsp.so" "$ROOT/drizzle-module.tar.gz" "$BUILD/drizzle.html"
echo "built $ROOT/drizzle-module.tar.gz and $BUILD/drizzle.html"
