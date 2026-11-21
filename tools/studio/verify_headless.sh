#!/usr/bin/env bash
# Dependency-light AIVERSE Studio validation. Does NOT prove Metal/WebView parity.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
for executable in node npm python3 c++; do
  command -v "$executable" >/dev/null 2>&1 || { echo "Missing: $executable" >&2; exit 2; }
done
NODE_MAJOR="$(node -p 'Number(process.versions.node.split(".")[0])')"
if [[ "$NODE_MAJOR" -lt 22 ]]; then
  echo "Node.js 22+ required, found $(node --version)" >&2
  exit 2
fi
echo "==== ImGui boundary ===="
python3 tools/studio/migration_audit.py --strict --json
echo "==== Python Studio tests ===="
python3 -m unittest discover -s tools/studio -p 'test_*.py' -v
echo "==== Pure TypeScript domain tests (no npm install) ===="
(cd apps/studio-web && npm run test:web)
if command -v tsc >/dev/null 2>&1; then
  echo "==== Strict standalone TS type check ===="
  tsc --noEmit --strict --target ES2022 --module ESNext \
    --moduleResolution Bundler --lib ES2022,DOM \
    apps/studio-web/src/dockLayout.ts \
    apps/studio-web/src/nestedDock.ts \
    apps/studio-web/src/propertyLogic.ts \
    apps/studio-web/src/panels/materialSchema.ts \
    apps/studio-web/src/panels/lightingSchema.ts \
    apps/studio-web/src/panels/shaderGraphLayout.ts
else
  echo "Global tsc unavailable, standalone check skipped (Node tests still run)"
fi
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
for item in StudioViewportLayoutTests StudioLightingValidationTests \
  StudioMaterialValidationTests StudioShaderGraphValidationTests \
  StudioFpsLightingContractTests StudioLightPreviewMathTests
do
  echo "==== C++17 contract: $item ===="
  c++ -std=c++17 -Wall -Wextra -Werror -pedantic -I . \
    "apps/studio-native/tests/$item.cpp" -o "$TEMP_DIR/$item"
  "$TEMP_DIR/$item"
done
echo "Headless contracts PASS. macOS Metal / React Vite still require separate builds."
