# Structure appearance in Rules and order marker authoring

Stage e8f5ca941400a4906947fa4fe7dd9bff8dbe7d77 on feature/aiv199_order_upgrade_view. The Rules panel exposes types.<id>.view.showUpgradeTarget for types whose resolved rules contain a structure and upgrades. The existing native studio.rulesSet path persists, validates, and resets the property. Use the Overlays panel's local_order_ring template to preview a local command marker. Overlay writes now use project document history and roll back the in-memory overlay declaration when the disk transaction fails.

Paths: apps/studio-web/src/panels/rulesSchema.ts, apps/studio-web/tests/rulesSchema.test.mjs, apps/editor/core/OverlayOps.cpp, apps/studio-native/tests/StudioSessionTests.cpp and both feature manifests.

Verification: three Rules schema Node tests passed, including inheritance and filtering. TypeScript compiled and Vite produced a bundle on macOS with 1,836 modules. The first standalone frontend copy failed to resolve the branding image at its repository-relative path; rebuilding from the worktree with --outDir on KINGSTON passed. The workflow test through studio.rulesSet, overlay_set and studio.projectHistoryStep is implemented; native execution and rendering remain pending.
