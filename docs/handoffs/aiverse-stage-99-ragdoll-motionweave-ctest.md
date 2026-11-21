# AIVERSE Stage 99 — First CTest MotionWeave and Ragdoll failures

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Verified starting SHA: 5e10db715bdb7cf55c30b701a55c20e9a1a3ebe0
Previous: docs/handoffs/aiverse-stage-98-modelwindow-stale-filesystem-field.md

## User supplied incomplete CTest log
The clean worktree started 201 registered tests, and 200 had reported as of
the pasted log; no final summary or completion has been seen yet.
At least nine failures were already visible. Three bounded issues fixed
here; other GPU, source dependency and skills errors are investigated
separately. Do NOT claim all tests passed.

## Changes
- libraries/include/aiverselib/animation/RagdollAsset.h:
  Chest capsule now ends at Head when the authored rig lacks Neck
  but has a Head. This makes Hips->Chest, Chest->Head and Head sphere
  available for compact humanoids and maintains two parent joints.
  Unchanged for full rigs with explicit Neck.
- tests/animation/motionweave_cost.cpp: its simple Bind graph has estimated
  safe cost below scheduler's default 256 threshold, so worker dispatch
  is correctly suppressed. Set minimumParallelCost=1 in this TEST only,
  preserving the default production scheduler policy, and verify actual
  parallel dispatch when workerCount()>0.
- tests/animation/motionweave_source_bridge.cpp:
  the initial one-item sync was mistakenly called with removeAbsent=true
  and removed the second existing entity, contradicting the immediate
  expectation of 2 unchanged links following the invalid duplicate
  snapshot. Change only that preflight sync to removeAbsent=false; the
  subsequent explicit removal regression remains intact.

## Validation
GitHub source read+exact-anchor transformations performed. No local native
compile, CTest rerun, or GPU verification performed by agent. Retain a
full CTest log and compare failures against exact updated HEAD.

## Next
Inspect other reported failures: GPU waitForResourceIdle test device
fixtures; model dependency library use detection; skill catalog routing
and audit metadata. Update with a separate handoff before next CTest.
