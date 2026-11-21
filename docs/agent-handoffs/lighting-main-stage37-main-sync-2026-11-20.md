# Lighting Stage 37 — concurrent main synchronization

2026-11-20, main. Code merge 73c5849b9ba7ac77404cdbdcc4b4e3b129cea800.
Parents: Stage36 handoff 6b6553dbf and fetched origin/main
7d7bd7c991a42f48ceb1105045da2ed942d69e61. Stage36 lighting merge remains
9611d05d5aaf299f0892b1fe2f60b935a6c5bede; all feature history is retained.

Latest peer changes add opt-in structured fog/billows and performance handoffs.
Resolved append conflicts in AGENTS.md and PRISMA handoff by retaining both.
HdrScene.h combines fog landscape setup/draw/cleanup with lighting,
shadow-diffusion, DynamicGiScene and Luma fixtures. No additional API/save/default
changes beyond source branches. Root dirty checkout intact.

Actually executed on KINGSTON, same root as Stage36:
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-main-merge-20261120
- cmake --build <root>/build --parallel 12 --target aiverse_hdr_scene
  aiverse_studio aiverse_studio_metal_lighting_tests aiverse_local_fog_tests:
  PASS (logs/main-sync-build.log). TMPDIR and CLANG_MODULE_CACHE_PATH on KINGSTON.
- AIVERSE_HDR_VERIFY=1 <root>/build/aiverse_hdr_scene: exit 0,
  54/54 Metal HDR probes PASS (logs/main-sync-hdr.log).
- ctest --test-dir <root>/build -R
  '^(local_fog|studio_metal_lighting|lighting_(dynamic_gi_gpu|compute_gi_gpu|
  icb_gi_gpu|icb_compute_gi_gpu|terrain_gi_gpu|graph_gi_gpu|luma_gpu|
  virtual_material_gi_gpu))$' --output-on-failure: 10/10 PASS in one fresh run,
  14.07s (logs/main-sync-tests.log). Earlier Stage36 MCP/web/CPU/host tests cover
  unchanged sources; not rerun without relevant changes.
- git diff --cached --check PASS. Preliminary ctest -N without external-write
  escalation could not create its log; actual tests used proper access and passed.

No full-suite/Release/mobile/Vulkan-hardware claim. CI NOT CHECKED.
Publication BLOCKED by automatic approval review: it rejected the command
containing git push origin main because user explicitly authorized merge but
review requires explicit permission to publish to the remote default branch.
The rejected combined command did not execute. Local merge is complete;
this handoff is committed independently. Ask for explicit push authorization,
then normal push and remote ancestry check; never force-push or bypass review.

## Publication confirmed — 2026-11-20

User explicitly approved: “Да, отправляй main”. The approval blocker above
is resolved. Normal git push origin main succeeded (no force), advancing remote
7d7bd7c99 to 8dcb2528df7c342626af4cb073f7743e4d0b85fc. git ls-remote
confirmed that exact main SHA; git merge-base --is-ancestor confirmed lighting
merge 9611d05d5aaf299f0892b1fe2f60b935a6c5bede is included. Working tree clean
before this documentation checkpoint. No source changes or new builds; previous
verification remains applicable. CI not checked.
