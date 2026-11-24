# SpectraFX Stage 123 — full-build warning fixes / retest pending

Branch: `feature/spectrafx-html5-data-interfaces-20261123`
Code HEAD: `f1cd4d536e99ff18c18badff57c56b64ca12c4c9`
Prior evidence: [Stage 122](spectrafx-studio-stage122-mac-acceptance-2026-11-24.md), tested code 2722bc82dbff78e1c3164a0714e595d4326ff3f9.

## Scope and status
The remaining Mac integration gate is a full engine build with warnings as errors, then full CTest. Focused SpectraFX, Metal hardware and CPU/GPU parity were green on the earlier tested SHA only. Full build had failed; full CTest was skipped. Subsequent commits:
- 90f28b55e802a20e15c688d0450d8471141c1cb2 — initialize VIVID synthetic nodes.
- 594f1c17b3bd8219f39facca3978368b74631828 — remove unused race fixture root parameter.
- 51df6660eca2f378d876ff8a8b1ba983b736931b — remove unused FPS test accessors.
- f1cd4d536e99ff18c18badff57c56b64ca12c4c9 — replace Lua inspector test keyword macro with scoped friend fixture.

No new runtime, format, authoring API, dependencies or platform claims in this handoff; no new source change. Existing feature contract and Stage 121 limitations remain in force. GitHub Actions query for the code SHA returned no runs. Native compilation, full CTest, current-SHA focused GPU verification, Vulkan and real Studio interactions are NOT RUN in this continuation. No merge to main, and no main synchronization attempted while collecting pinned QA evidence.

## Resume on Mac
Reuse the isolated source and build on KINGSTON:
```bash
ROOT=/Volumes/KINGSTON/AIVERSE-builds
SRC="$ROOT/spectrafx-isolated-src-20261124-131615"
BUILD="$ROOT/spectrafx-isolated-build-20261124-131615"
cd "$SRC"
test -z "$(git status --porcelain)" || exit 1
git fetch origin "feature/spectrafx-html5-data-interfaces-20261123"
git switch --detach f1cd4d536e99ff18c18badff57c56b64ca12c4c9
export AIVERSE_ACCEPTANCE_JOBS=8
export AIVERSE_ACCEPTANCE_WARNINGS_AS_ERRORS=1
export AIVERSE_ACCEPTANCE_REQUIRE_METAL=1
bash tools/spectrafx/acceptance.sh "$BUILD"
```
Archive logs including summary.md and full build/CTest logs even on failure. Do not reuse stale binaries if the build fails. Preserve the existing cache and arm64 configuration; do not relax warnings or Metal requirement. The runner source was reviewed through GitHub but not executed here.

Next: inspect actual archive; fix only evidenced build/test failures, keeping other agents' ownership. After green Mac full acceptance, separately verify Vulkan and actual Studio workflows; merged-tree Mac quick gate remains required before main.
