# minimap-background-packaging — initial handoff

Owner: build
Branch: `codex/issue223-minimap-atlas-20261126`
Stage commit: `7aaf3bf9a5f115e707cbcf19fe396960bfd3753e`
Paths: `tools/atlas/AtlasBuild.cpp`, `tools/atlas/AtlasBuild.h`, `tests/tests.cpp`, `docs/game-format.md`, `docs/devlog/systems.json`, `docs/agents/feature-policy.json`, `docs/features/minimap-background-packaging.json`.
Architecture/API: the existing atlas input scan records top-level `minimapBackground` references from game and map JSON. The packer leaves those source images intact for the existing CPU minimap decoder. No new public API.
Tests: AppleClang 17 release build of `aiverse_tests` on macOS completed. `/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-make/aiverse_tests atlas_preserves_raw_minimap_backgrounds`: 11 checks, 0 failed. Evidence: `docs/evidence/minimap-background-packaging-20261126.json`. The first test run failed due to an invalid assertion about whether an unrelated icon would be atlased; the corrected regression passed.
Risks: packaged client rendering and non-Mac hosts are not tested. The corrected image remains as an original PNG, increasing package size by that image's bytes.
Next: run the merged-tree Mac quick gate, validate the feature policy and site roadmap, then integrate with a squash after readiness review.

## 2026-11-26 — shared image loading revision (uncommitted)

User rejected consumer-specific rules in AtlasBuild. Removed the minimapBackground scan and standalone exception. Added render::loadContentImage using the existing atlas::ImageSource reconstruction and standalone decoding, and routed Minimap through it. Removed the obsolete exception regression; existing atlas reconstruction tests remain. Previous 11-check packaging result does not validate this revision. Build/tests NOT RUN: user requested one final combined build. Next: add consumer regression, review shared image API, and validate the final tree.
