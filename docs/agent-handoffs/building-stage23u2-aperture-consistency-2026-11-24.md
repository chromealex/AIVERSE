# AIVERSE buildings — Stage 23U.2: single-slab authored progress consistency

Date: 2026-11-24
Target branch: feature/aiv20261123_building-grammar
Target PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)
Candidate parent: ab0903042d2f2eaf65fd9ae6cb907892b9fce807

## Source review and implementation

Stage 23U.1 introduced authored fractional initial progress but the single-slab BuildingApertureOwner still inserted the original closed AABB. Its advanceMotion compared the registered collider against the translated AABB at the saved progress, so the first step from a half-open recipe was rejected as ownership loss. The binary open check also made closing a partially-open slab a no-op or invalid. SpatialWorld::add replaces existing IDs; initial single-slab attach lacked a preflight lease check.

- engine/buildings/BuildingApertureOwner.h: use the same physicalBounds formula for initial insertion and stepped motion; reject invalid authored open fractions and pre-owned IDs; check physical endpoints in beginMotion and setOpen; verify exact owned collider shape before direct operations; cover old and closed bounds when closing from a partial position; preserve committed progress on obstruction.
- tests/libraries/building_dual_leaf_restore_tests.cpp: add a single-slab fixture to the already wired focused regression, using real SpatialWorld. Covers initial half-open AABB/readback, blocked sweep, five closing steps, full-open removal, immediate close from authored partial state, collision ID conflict. Existing dual-leaf zero-revision regression retained.
- skills/world/aiverse-building-style/SKILL.md: document shared physical-progress contract.
- No genre modules, second ECS, new transport, or renderer ownership changes. Logical open/access still requires the full endpoint and graph acknowledgement. Real actor/cargo passage and graph refresh require host integration and validation.

## Evidence

Actual checks: read the exact parent source and SpatialWorld::add implementation; checked live PR HEAD and workflow/job states. Linux arm64 gcc job 138236694049 was in Build. Focused jobs 138236692977 and 138236693742 were queued, and HTML5/ECS jobs were queued. No successful build/test/capture was available for this SHA. The new regression is authored, NOT executed. The branch HEAD is intentionally unchanged while CI runs.

## Next steps

1. Complete the current exact-head CI checks; inspect real errors and artifacts. Do not repeatedly cancel queued jobs.
2. Reconcile this candidate with any newer HEAD, apply it to the same feature branch once appropriate, and rerun focused C++ regression and GLB/PBR validation.
3. Verify shared Simulation host registration, rollback graph ACK, actor/cargo physical extents and collider ID leases; validate native Studio Metal/Vulkan/GLES capture and GPU retirement.
4. Keep PR draft until full acceptance and main reconciliation. Record actual results in the next handoff.
