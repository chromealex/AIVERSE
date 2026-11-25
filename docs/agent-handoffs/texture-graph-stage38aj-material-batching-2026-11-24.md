# Texture Graph GPU — Stage 38AJ
Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `ed9aa56437a7fca91c1dfee10c750631866d1d60`

## Stage 38AJ — Material Batching CPU acceptance and provenance (2026-11-24)

Implementation: `f82db744f747226138770a949bcfb13e62613cb8`. Desktop normal test builds now include the existing material batching executable and register `material_batches_unit` with `--unit`; HDR-only builds retain the visual A/B executable. Targeted Stage 38 runner adds `--material-batches` (32 tests versus prior 31), checks CMake source identity, prints Git HEAD and tracked status, rejects skips and incomplete CTest counts and detects changed HEAD/status. No runtime API/save format changes; no new GPU behavior. Exact-commit source checks 10/10 PASS; no PR-triggered Actions runs returned for implementation SHA. Native compilation, 32-test Mac acceptance, live Studio/MCP, Vulkan hardware and merged-tree Mac gate NOT RUN here. Reconfigure the existing external build before the targeted runner so the newly added CTest registration exists. Do not treat dirty status as clean-checkout evidence.

Paths: CMakeLists.txt, tools/texture-graph-stage38-acceptance.py, docs/features/graph-framework.json, docs/devlog/systems.json.

Next on the existing Mac worktree (do not share CMake cache with other sources):

```bash
git pull --ff-only
BUILD=/Volumes/KINGSTON/AIVERSE-builds/texture-graph-stage38-arm64
cmake -S . -B "$BUILD"
set -o pipefail
python3 tools/texture-graph-stage38-acceptance.py --build-dir "$BUILD" --backend metal --parallel 4 --material-batches 2>&1 | tee "$BUILD/texture-graph-stage38aj.log"
```

Capture the exact printed SHA, CTest 32/32, and material_batches_unit outcome. Next live Studio/MCP smoke, then Vulkan hardware and merged-tree Mac quick gate. PR #133 remains unmerged; no main update.
