#!/usr/bin/env bash
# Stage 38BG: matching web, native, strict Metal acceptance and MCP audit.
set -euo pipefail
source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$source_dir"
if [[ "$(uname -s)" != Darwin ]]; then echo "This runner requires macOS and real Metal hardware" >&2; exit 2; fi
[[ -d /Volumes/KINGSTON ]] || { echo "Mount KINGSTON first" >&2; exit 2; }
sha="$(git rev-parse HEAD)"
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "Use a clean tracked checkout" >&2; exit 2; }
jobs="${AIVERSE_JOBS:-8}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid AIVERSE_JOBS" >&2; exit 2; }
root="/Volumes/KINGSTON/AIVERSE-builds/texture-graph-38bg-${sha:0:12}-$(uname -m)"
mkdir -p "$root"
logs="$(mktemp -d "$root/qa-logs.XXXXXX")"
finish(){
    local status=$?
    trap - EXIT
    printf 'exit=%s\nsha=%s\n' "$status" "$sha" > "$logs/result.txt"
    local archive="$root/texture-graph-38bg-${sha:0:12}-$(date -u +%Y%m%dT%H%M%SZ)-$$.tar.gz"
    tar -czf "$archive" -C "$logs" .
    echo "QA archive: $archive"
    exit "$status"
}
trap finish EXIT
run(){
    local step="$1";shift
    printf '\n========== %s ==========\n' "$step"
    "$@" 2>&1 | tee "$logs/$step.log"
}
{ git rev-parse HEAD; git status --short; uname -a; cmake --version; node --version; npm --version; } | tee "$logs/environment.log"
node -e 'if(Number(process.versions.node.split(".")[0])<22)process.exit(1)' || { echo "Node 22+ required"; exit 2; }
web_root="$(mktemp -d "$root/web.XXXXXX")"
git archive HEAD apps/studio-web | tar -xf - -C "$web_root"
web="$web_root/apps/studio-web"
export npm_config_cache="$root/npm-cache"
run npm-install npm --prefix "$web" install --no-audit --no-fund
run web-typecheck npm --prefix "$web" run typecheck
run web-tests npm --prefix "$web" run test:web
run web-build npm --prefix "$web" run build
build="$root/native"
run configure cmake -S "$source_dir" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=ON \
    -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_STUDIO_HTML5=ON \
    -DAIVERSE_TEXTURE_GRAPH_GPU_TESTS=ON -DAIVERSE_STUDIO_WEB_DIST="$web/dist"
run studio-mcp-build cmake --build "$build" --config Release --parallel "$jobs" --target aiverse_studio aiversemcp
run native-acceptance python3 tools/texture-graph-stage38-acceptance.py --build-dir "$build" --backend metal --full --parallel "$jobs"
mcp="$(find "$build" -type f -name aiversemcp -perm -111 -print | sed -n '1p')"
[[ -n "$mcp" ]] || { echo "MCP executable not found"; exit 1; }
run mcp-audit python3 tools/mcp/audit_registry.py "$mcp" --engine "$source_dir" --feature-manifest docs/features/graph-framework.json
[[ "$(git rev-parse HEAD)" == "$sha" && -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "Checkout changed during QA"; exit 1; }
app="$(find "$build" -type d -name AiverseStudioHTML5.app -print | sed -n '1p')"
[[ -n "$app" ]] || { echo "Studio bundle not found"; exit 1; }
printf 'STUDIO_APP=%s\n' "$app" | tee "$logs/studio-app.txt"
echo "Automated acceptance passed; live Studio/MCP, Vulkan hardware and merged-tree gate remain separate."
