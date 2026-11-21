# AIVERSE LENS Stage 48B.3A.1 — protect unbuilt/planet height fallback

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (main untouched)
Parent: c354a51678952ba6a8c80a54fc590d16a0bdd1c5
Previous: docs/agent-handoffs/lens-motionweave-integration-stage48b3a-atomic-editor-sculpt-published-height-2026-11-20.md

## Changed source

- `modules/rts/view/RtsTerrain.cpp`: after Stage48B.3A introduced immutable published height snapshots, `RtsView::heightAt` must continue to use the old Rules-derived height calculation when no Terrain mesh has yet been built or when the view is in planet mode. In those two conditions return `terrain::heightAt(grid, rules_->levelHeight, x,z)`; only a built flat-map Terrain uses `terrain_.surfaceAt()` to guard against in-place editor mutations before the next atomic commit.
- This prevents a regression for a nondefault rules.levelHeight before the first Terrain build, and prevents a stale published flat-map height snapshot being applied when the same view is used for planet/cube-sphere rendering.

## Test added (NOT RUN)

- `tests/tests.cpp`: `lens_stage48b3a1_unbuilt_terrain_uses_rules_level_height` creates a Rules with levelHeight=4, initializes RtsView without calling buildTerrain, and checks a level-1 ground cell yields height 4 before teardown.
- Planet/cube-sphere runtime parity remains a separate pending GPU test, and is not claimed tested by this fixture.

## Actual verification

Unique-anchored GitHub source edit, new test source and handoff are committed together, and source refetch + HEAD verification performed. No native CMake, C++ compiler, CTest, Metal/Vulkan/GLES or runtime render test performed; test is authored but unexecuted.

## Next

Stage 48B.3B: scene-aware unpublished Terrain topology + GroundFX, Road, Shore, Fog and camera resources, with fully versioned input snapshot and no duplicate preparation. Then Sprite/MotionWeave, lighting and overlays; consolidated native and GPU backend checks before main integration.
