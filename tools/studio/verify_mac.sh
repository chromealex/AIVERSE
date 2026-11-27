#!/usr/bin/env bash
# One-pass validation of experimental AIVERSE Studio HTML5 on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT_ROOT="${AIVERSE_STUDIO_VERIFY_ROOT:-/Volumes/KINGSTON/AIVERSE-builds/studio-html5-verify}"
if [[ ! -d /Volumes/KINGSTON || ! -w /Volumes/KINGSTON ]]; then
  echo "KINGSTON must be mounted and writable; no internal-disk fallback." >&2
  exit 2
fi
case "$OUTPUT_ROOT" in
  /Volumes/KINGSTON/AIVERSE-builds/*) ;;
  *) echo "Studio QA output must remain on KINGSTON." >&2; exit 2 ;;
esac
BUILD_DIR="$OUTPUT_ROOT/native"
# The app launched from this folder is the working Studio, so it is optimized by
# default (-O2 with debug symbols). A -O0 Debug Studio redraws the viewport
# 5-20x slower. STUDIO_BUILD_TYPE=Debug ./verify_mac.sh for a debugger session.
STUDIO_BUILD_TYPE="${STUDIO_BUILD_TYPE:-RelWithDebInfo}"
WEB_DIST="$OUTPUT_ROOT/web"
mkdir -p "$OUTPUT_ROOT/tmp"
export TMPDIR="$OUTPUT_ROOT/tmp"
export CLANG_MODULE_CACHE_PATH="$OUTPUT_ROOT/clang-modules"
export npm_config_cache="$OUTPUT_ROOT/npm-cache"
export PYTHONDONTWRITEBYTECODE=1
cd "$ROOT"

if [[ "$(uname -s)" != Darwin ]]; then
  echo "This native build requires macOS, WKWebView and the Apple Metal SDK." >&2
  exit 2
fi
for required in npm cmake ctest xcrun; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "Missing dependency: $required" >&2
    exit 2
  fi
done

echo "==== Step 1: HTML5 TypeScript/Vite ===="
(
  cd "$ROOT/apps/studio-web"
  npm install --no-audit --no-fund
  npm run typecheck
  npm run test:web
  ./node_modules/.bin/tsc --incremental --tsBuildInfoFile "$OUTPUT_ROOT/studio.tsbuildinfo"
  ./node_modules/.bin/vite build --outDir "$WEB_DIST"
)
if [[ ! -f "$WEB_DIST/index.html" ]]; then
  echo "Vite build did not produce dist/index.html" >&2
  exit 1
fi
echo "==== Step 1b: verify bundled ES module asset paths ===="
AIVERSE_STUDIO_WEB_DIST="$WEB_DIST" python3 -m unittest discover -s tools/studio -p 'test_*.py' -v

echo "==== Step 2: CMake configure ===="
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_STUDIO_WEB_DIST="$WEB_DIST" \
  -DCMAKE_BUILD_TYPE="$STUDIO_BUILD_TYPE"

echo "==== Step 3: native target and tests ===="
cmake --build "$BUILD_DIR" \
  --target aiverse_studio_verification \
  --parallel 4

echo "==== Step 4: studio contracts ===="
ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error \
  --output-junit "$OUTPUT_ROOT/studio-ctest.xml" -R '^studio_'

echo "==== Automated verification finished; macOS visual testing still required ===="
echo "Open the generated AiverseStudioHTML5.app with --project /path/to/project."
echo "Manually verify Metal rendering, resizing, picking, history and native popovers."
