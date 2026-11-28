#!/usr/bin/env bash
# Prepare the HTML assets required by desktop acceptance builds.
set -euo pipefail
cd "$(dirname "$0")/../../apps/studio-web"
if [[ ! -x node_modules/.bin/vite ]]; then
  npm install --no-package-lock --no-audit --no-fund
fi
npm run build
