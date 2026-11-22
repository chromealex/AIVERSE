#!/bin/bash
# AIVERSE Studio on macOS: builds the web UI and the native Studio, then opens it.
# Usage: ./studio_mac.command [project folder]   (default: the project hub's last project / templates/skirmish)
# Log: studio_mac.log. Needs Xcode, CMake and Node.js (brew install cmake node).
cd "$(dirname "$0")" || exit 1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
PROJECT="${1:-templates/skirmish}"
{
  echo "== $(date)"
  command -v node >/dev/null || { echo "Node.js is missing: brew install node"; exit 1; }
  command -v cmake >/dev/null || { echo "CMake is missing: brew install cmake"; exit 1; }
  echo "== web"
  ( cd apps/studio-web && { [ -d node_modules ] || npm install --no-audit --no-fund; } && npx vite build ) || { echo "WEB BUILD FAILED"; exit 1; }
  GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
  echo "== configure"
  cmake -S . -B build/mac $GEN -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_STUDIO_HTML5=ON || { echo "CONFIGURE FAILED"; exit 1; }
  echo "== build"
  cmake --build build/mac --target aiverse_studio -j 8 || { echo "BUILD FAILED"; exit 1; }
  echo "== open"
  APP=build/mac/apps/studio-native/AiverseStudioHTML5.app
  open "$APP" --args --project "$(cd "$PROJECT" 2>/dev/null && pwd || echo "$PROJECT")"
  echo "== done: $APP"
} 2>&1 | tee studio_mac.log
