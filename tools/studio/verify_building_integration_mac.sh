#!/usr/bin/env bash
# Stage 37: incremental Building/Studio acceptance, reusing an existing Mac quick build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
VOLUME=/Volumes/KINGSTON
if [[ "$(uname -s)" != Darwin ]] || ! mount | grep -Fq " on $VOLUME (" || [[ ! -w "$VOLUME" ]]; then
  echo "Mounted writable KINGSTON on macOS is required." >&2; exit 2
fi
BUILD_DIR="${1:-$ROOT/all-tests-v2}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"
case "$ROOT/" in "$VOLUME/"*) ;; *) echo "Source checkout is not on KINGSTON" >&2; exit 2;; esac
case "$BUILD_DIR/" in "$VOLUME/"*) ;; *) echo "Build is not on KINGSTON" >&2; exit 2;; esac
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] || ! grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$ROOT" "$BUILD_DIR/CMakeCache.txt"; then
  echo "Expected existing CMake cache for this exact source path; refusing to reset it." >&2; exit 2
fi
for exe in git python3 node npm cmake ctest; do command -v "$exe" >/dev/null || { echo "Missing $exe" >&2; exit 2; }; done
if [[ "$(node -p 'Number(process.versions.node.split(".")[0])')" -lt 22 ]]; then echo "Node 22+ required" >&2; exit 2; fi
OUT="$BUILD_DIR/stage37-studio"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd -P)"
case "$OUT/" in "$BUILD_DIR/"*) ;; *) echo "Output escapes build tree" >&2; exit 2;; esac
for sub in tmp npm-cache web reports; do
  if [[ -L "$OUT/$sub" ]]; then echo "Refusing symlink: $OUT/$sub" >&2; exit 2; fi
  mkdir -p "$OUT/$sub"
done
REPORT="$OUT/reports/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir "$REPORT"
export TMPDIR="$OUT/tmp/" npm_config_cache="$OUT/npm-cache" PYTHONDONTWRITEBYTECODE=1
JOBS="${JOBS:-8}"
cd "$ROOT"
run() { local name="$1"; shift; echo "========== $name =========="; "$@" 2>&1 | tee "$REPORT/$name.log"; }
trap 'rc=$?; echo "Reports: $REPORT (exit $rc)"; exit "$rc"' EXIT
run head git rev-parse HEAD
run mcp-build cmake --build "$BUILD_DIR" --target aiversemcp --parallel "$JOBS"
run mcp-registry python3 tools/mcp/audit_registry.py "$BUILD_DIR/aiversemcp" --engine "$ROOT" --feature-manifest docs/features/building-graph-integration.json
run mcp-building python3 tools/mcp/test_building_diagnostics_stdio.py "$BUILD_DIR/aiversemcp" "$ROOT"
(
  cd "$ROOT/apps/studio-web"
  if [[ -L node_modules ]]; then echo "Refusing linked node_modules; install on KINGSTON" >&2; exit 2; fi
  run npm-install npm install --no-audit --no-fund --package-lock=false
  run web-tests npm run test:web
  run web-build npm run build -- --outDir "$OUT/web"
)
test -f "$OUT/web/index.html"
export AIVERSE_STUDIO_WEB_DIST="$OUT/web"
run studio-python python3 -m unittest discover -s tools/studio -p "test_*.py" -v
# Keep the existing Debug configuration, compiler, architecture and CMake cache.
run configure cmake -S "$ROOT" -B "$BUILD_DIR" -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_STUDIO_WEB_DIST="$OUT/web"
run studio-build cmake --build "$BUILD_DIR" --target aiverse_studio aiverse_studio_building_topology_tests aiverse_studio_session_tests --parallel "$JOBS"
run studio-ctest ctest --test-dir "$BUILD_DIR" --config Debug --no-tests=error --output-on-failure --output-junit "$REPORT/studio-ctest.xml" -R "^(studio_building_topology|studio_session|studio_bridge|studio_material_schema)$"
APP="$BUILD_DIR/apps/studio-native/AiverseStudioHTML5.app"
test -x "$APP/Contents/MacOS/AiverseStudioHTML5"
echo "Automated Stage 37 checks PASS. Live owner MCP and visual QA NOT RUN."
echo "Studio app: $APP"
echo "Use an isolated project, inspect rooms/portals, route, texture provenance and live graph overlay."
