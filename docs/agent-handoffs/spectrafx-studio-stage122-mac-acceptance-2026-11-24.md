# SpectraFX stage 122 — Mac acceptance and full-build blocker

Evidence: user-supplied spectrafx-qa-v9.tar.gz, tested revision 2722bc82dbff78e1c3164a0714e595d4326ff3f9, Darwin, Apple M3 Ultra, isolated arm64 build on KINGSTON, warnings-as-errors enabled.

- Configure, targeted SpectraFX build, architecture, editor/client build: PASS.
- CTest SpectraFX: 23/23 passed, 0 failed, 28.98 s. Metal subset: 6/6 passed, 0 failed, 22.86 s. Required Metal evidence: 5/5 real PASS, zero skipped/missing/failed.
- GPU allocator: capacities 256/65536/262144 in procedural and nonprocedural paths; growth/shrink, live compaction, LOD, indirect pixels, owned checkpoint and shared budget PASS. Shared budget peak 524288 bytes, denied=2.
- CPU/GPU parity: 4096 particles, 180 frames, 1447 alive on each path, measured position/age errors zero. Image comparison at frames 30/60/90 max delta zero. GPU timing is fixture-specific, not whole-game performance.
- Full build failed in tests/vivid/metahuman_adapter.cpp on two missing-field-initializer diagnostics; full CTest intentionally skipped. This change initializes both synthetic Node fixtures explicitly without changing their values. Native retest of this correction pending.
- Vulkan, other platforms and full engine CTest not certified by this report. No merge to main.

Resume in existing isolated worktree/build directory; preserve -Werror and Metal requirement. Run full acceptance again and attach logs.
