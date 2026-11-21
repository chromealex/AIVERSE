# LENS Stage 48B.3B.3 — unpublished topology camera resources

Date: 2026-11-20. Branch: `codex/lens-completion`.
Parent: `9d6cb4e7e2767b1442d96eabbcca7c1f91119f7b`.
Implementation: `00f87a503068f4f4eae603a4d991ea4e20b2a34b`.
Previous: [Stage 48B.3B.2](lens-motionweave-integration-stage48b3b2-one-shot-topology-surface-lease-2026-11-20.md).

## Implementation

`modules/rts/view/RtsTerrainRender.{h,cpp}` adds `prepareMapVisible`:
the unpublished map retains a PreparedFrame built against its own geometry,
dynamic/vision pixels, visible detail LODs and trace heights. It borrows the
live shader kit; new pipeline misses remain owned by PreparedFrame.
`commitMap` transfers topology first and privately rebinds this exact frame
before committing it, without GPU uploads. Candidate rollback owns both.

`modules/rts/view/RtsView.{h,cpp}` stages the actual processed viewer Fog
against the new sheet, then prepares Terrain with that field. A consumed
topology lease returns an optional Fog-change result so primary draw skips
the redundant surface cohort. Stale input still uses the existing synchronous
fallback. No generic animation clock, simulation, save/network format, RHI,
public legacy build method or production default changed.

`tests/tests.cpp` adds failure/retry plus first-frame upload-count coverage,
and conditional trace-height transfer coverage using the existing fake RHI.
Tests are registered through the existing core suite.

## Verification

`git diff --check`: PASS before commit.
Native compilation, CTest, GPU screenshots, sanitizers: NOT RUN at this
feature-only checkpoint. CI: NOT RUN; no run URL. Authored tests are not a
passing result. The initial sandbox git add/commit could not write index.lock;
the authorized retry succeeded. No build has run on the internal drive.

All builds, intermediates, compiler caches, temporary compiler files and
logs MUST be under `/Volumes/KINGSTON/AIVERSE-builds/lens-completion` per
current user instruction. Directory exists; disk had about 746 GiB free.
Source worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/lens-completion`.

## Remaining work

This completes the camera-resource prewarm portion, not all LENS.
A hard transient topology GPU budget is still absent; existing 16-visible-LOD
and GroundFX budgets remain. Sprite/MotionWeave/Lighting/Overlay full owner
transaction integration remains to be audited/finished. Native build and
adversarial tests must validate private frame ownership, missing shader tiers,
late source mutations and resized Fog before release. No merge/push performed.

Next: audit remaining frame owners and topology allocation budgets, then run
native validation on KINGSTON and repair actual failures. Do not claim GPU
parity or frame-time improvement from source or fake-RHI counters.
