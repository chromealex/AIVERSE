# LENS topology preparation allocation budget — 2026-11-20

Branch: codex/lens-completion. Implementation: `481c07e85c20ddae22785674c91ba9d90ae886a4`.
Previous: lens-native-validation-2026-11-20.md and Stage48B.3B.3.

Added generic engine/render/PreparationAllocationBudget.h: owner-thread shared
ledger and RAII leases, overflow-safe admission, aggregate outstanding cap.
TerrainRenderer prepareMap uses a 128 MiB default logical payload limit shared
by all outstanding candidates; caller can setTopologyPreparationBudgetBytes.
Nested full/detail rebuilds share the candidate lease. Charges happen before
RHI allocations for meshes, conservative 32-bit water indices, RGBA8 mip chains,
dynamic/vision COW fields and trace height. Commit releases preparation charge;
rollback releases it when the candidate dies. Failed/repeated allocations retain
a conservative charge until candidate disposal. Ordinary live build is unchanged.

This is NOT a physical VRAM/driver-allocation cap, nor a global scene/retirement
memory cap: published/retired resources and shader/pipeline backend storage are
excluded. Other frame owners retain their separate budgets. Full Sprite/
MotionWeave/Lighting/Overlay rollback and real GPU validation remain open.
No save format change; additive Terrain budget APIs only.

Paths: new budget header, RtsTerrainRender.h/.cpp, RtsTerrainDetailMesh.cpp,
tests/tests.cpp. Tests cover overflow/shared leases, zero-budget rejection before
GPU calls, aggregate candidates, prewarm rejection, rollback/retry and commit
release for coarse and smooth maps. git diff --check PASS. Native tests for this
stage NOT RUN yet; earlier core binary is still being tested. CI NOT RUN.

All builds, temporary compiler files/caches/logs remain on KINGSTON.
Next: finish prior core run (source 6a5d3bf6d, report 20261120-151300-62204),
rebuild aiverse_tests, run lens_preparation_budget/lens_topology filters; inspect
all failures before claiming readiness. See validation handoff for existing
380 LENS checks PASS and remaining full-suite gates.

## Review correction

Commit `fcf72fafd934b226c3878684000d61a001baf263`: Terrain visibility is R8, so the allocator must admit
R8 with one byte/pixel as well as RGBA8 with four. The initial source gate
would have rejected valid map preparations (caught by review before tests).
Added RtsView setTerrainTopologyPreparationBudgetBytes and usage accessor so
hosts can tune the limit without accessing the private renderer. No save change.
Previous aiverse_game_view native build PASS; this correction and budget tests
NOT RUN yet. git diff --check PASS. All build outputs on KINGSTON.
Next: rebuild aiverse_tests and exercise both smooth/coarse topology tests
after the earlier full core binary has completed. CI NOT RUN.


## Native verification checkpoint — 2026-11-20

Budget code `481c07e85` plus correction `fcf72fafd` verified in the final native
Debug x86_64 build on branch `codex/lens-completion`: `aiverse_tests lens_`
PASS, 408 checks, including shared lease/overflow and coarse/smooth rejection,
rollback/retry, commit release. Log on KINGSTON:
`/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug/test-reports/lens-budget-42b7fe2e0.log`.
Final related CTest selection PASS 77/77 at `21d1617b1`;
report `20261120-154324-94687`. Full build PASS; see native-validation handoff
for exact commands and full-suite failures with targeted repairs. CI/real GPU/
ARM64 NOT RUN. Logical preparation budget exclusions above still apply.
Next: staged remaining frame owners and full publication rollback, then actual
Metal/Vulkan checks. Full LENS is not yet complete.
