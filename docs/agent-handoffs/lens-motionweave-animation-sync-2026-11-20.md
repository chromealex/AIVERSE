# AIVERSE LENS + MotionWeave — animation sync merge (2026-11-20)

## Git provenance
- Target branch: `feature/lens-motionweave-integration`.
- Merge commit: `8de34988294b35d559085319d0003316bec93c5c`.
- Parents, in order: LENS integration `2a1a841f6260c70bcc673449e8767febb53a1af5`, animation `44237cd8b07a408fa72339747ab869293e6faa7e`.
- This is a genuine two-parent merge commit, NOT a copy/replacement or a PR into main.
- Reconciled against original animation base `7c04338019a80bf6d82150f7db9ad791872c6adc`. Recursive Git trees showed exactly 10 newer animation files, all without overlapping edits in integration. Used the animation blobs for these 10 entries and preserved all other LENS integration blobs.

## Synced files
- `AGENTS.md`
- `apps/editor/animation/AnimationEditor.h`
- `apps/editor/windows/AttachmentAuthoringWindow.cpp`
- `apps/editor/windows/ModelingWindow.cpp`
- `apps/modelview/main.cpp`
- `docs/agent-handoffs/animation-stage154-modelview-attachment-workflow-2026-11-19.md`
- `docs/agent-handoffs/animation-universal.md`
- `docs/modelview-attachments.md`
- `docs/motionweave.md`
- `docs/view.md`

## Preserved LENS and animation architecture
- `engine/view/View.h` retains the LENS `cameraScope` isolation and independent LOD hysteresis because the new animation changes did not alter this file.
- `engine/render/MotionWeaveCameraFrame.h`, `CameraVisualFrame.h`, generic LENS ECS/scheduler/RT components and adapters remain present.
- MotionWeave's generic View, model source publication, attachment authoring, native ModelView and latest handoff are preserved.
- `main` and source animation branch not modified.

## Verification actually performed
- Read and compared three recursive Git trees; validated zero overlap conflicts among new animation file changes; created tree and two-parent merge via GitHub Git Objects API with an expected-head lease; read back branch head and both parents; inspected `cameraScope` code and AttachmentAuthoringWindow in merged branch.
- **NOT RUN:** native CMake configure/build, CTest, Metal/Vulkan/GLES GPU tests, Studio, GameClient or ModelView runtime. Do not claim the merge builds.
- The integrated LENS+MotionWeave branch may still have pre-existing compile or functional issues from earlier integration stages unrelated to this zero-conflict sync.

## Next work
1. Continue prepared immutable render-packet capture in the animation team's View / RtsView presentation path, including attachment lifetimes, so secondary cameras display animated models without repeating event processing.
2. Finish per-camera visual/material binding and event invariance tests.
3. Run one consolidated native build/test pass, fix errors and record actual results before merging to `main`.
