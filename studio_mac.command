#!/bin/bash
# AIVERSE Studio on macOS: builds the web UI and the native Studio, then opens it.
# Usage: ./studio_mac.command [project folder or .aiverse file]   (omit to reopen the last project)
# Log: studio_mac.log. Needs Xcode, CMake and Node.js (brew install cmake node).
set -o pipefail
PROJECT=""
if [ -n "${1:-}" ]; then
  if [ -d "$1" ]; then
    PROJECT="$(cd -- "$1" && pwd -P)" || exit 1
  elif [[ "$1" == *.aiverse && -f "$1" ]]; then
    PROJECT_DIR="$(cd -- "$(dirname -- "$1")" && pwd -P)" || exit 1
    PROJECT="$PROJECT_DIR/${1##*/}"
  else
    echo "Expected a project folder or .aiverse file: $1" >&2
    exit 1
  fi
fi
cd "$(dirname "$0")" || exit 1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
{
  echo "== $(date)"
  command -v node >/dev/null || { echo "Node.js is missing: brew install node"; exit 1; }
  command -v cmake >/dev/null || { echo "CMake is missing: brew install cmake"; exit 1; }
  echo "== web"
  ( cd apps/studio-web && { [ -d node_modules ] || npm install --no-audit --no-fund; } && npx vite build ) || { echo "WEB BUILD FAILED"; exit 1; }
  GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
  echo "== configure"
  cmake -S . -B build/mac $GEN -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_EDITOR=OFF "-DAIVERSE_STUDIO_WEB_DIST=$PWD/apps/studio-web/dist" || { echo "CONFIGURE FAILED"; exit 1; }
  echo "== build"
  cmake --build build/mac --target aiverse_studio -j 8 || { echo "BUILD FAILED"; exit 1; }
  echo "== open"
  APP=build/mac/apps/studio-native/AiverseStudioHTML5.app
  if [ -n "$PROJECT" ]; then
    open -n "$APP" --args --project "$PROJECT" || exit 1
  else
    open "$APP" || exit 1
  fi
  echo "== done: $APP"
} 2>&1 | tee studio_mac.log
