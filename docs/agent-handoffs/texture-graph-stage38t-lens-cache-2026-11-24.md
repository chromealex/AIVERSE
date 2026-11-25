# Stage 38T — LENS attachment ownership and cache isolation (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `b1c731452af8122d54788023af87aa5d0b4b1698`
Plan: `26bf7fe2b849e8257268b976b411bd42a16b7716`
Implementation: `732d4b7ae38f74cca74acfcd4a357ffa87f5406a`

Stage 38T: CameraModelSnapshot owns prepared attachment draw data (materials/pins, pose, node transforms, node material, baked sample and material mask), reconstructs pointers on replay/copy, enumerates attachment material roots. Also owns material masks for base/finalized packets. Model/View, LOD assets and GPU buffers remain host-owned. ModelTextureCache refuses runtime graph image names/metadata at file decode, resident borrowing, shared upload and promotion; commit rechecks procedural provenance. Model upload skips tombstones and does not reupload resident exclusive graph images. Generic addImage refuses procedural aliases; CPU image transaction refuses invalid exclusive ownership and same-name procedural collisions (resident or staged). Existing camera_model_snapshot and model_image_transaction fixtures extended for snapshot copy after source mutation, pin release, cache refusal and duplicate refusal. Seventeen exact-commit static assertions passed; neither fixture compiled or executed. Generic RHI streaming and remaining external raw index/handle contracts still require audit; no default-on collection.

## Actual evidence

Read back six exact committed source blobs from GitHub. Seventeen static checks returned true: attachment owned entries, capture copy, replay reconstruction, attachment root enumeration, material mask copy/reset/rebind, legacy provenance, cache decode/borrow/upload refusal, promotion prepare and commit guards, resident upload preservation, generic addImage alias refusal, exclusive staging validation, resident/staged name collision, camera source mutation and snapshot copy, camera pin release, cache regression fixture. These are static checks, not compilation or test execution. No new C++ tests, full build, native LENS, Metal/Vulkan, Studio/MCP, or Mac gate ran.

## Next

Stage 38U: audit remaining raw-index/handle consumers including native/editor preview and picking, generic RHI streaming ownership, renderer replay and external native projects. Determine whether prepared camera duplicate attachment material snapshots require profiling or shared immutable storage. Establish a complete owner-scoped contract before enabling default collection; do not replace the explicit external certificate with a constant true callback. Run all previously authored regression fixtures on complete checkout. Keep main untouched until merged-tree Mac gate and actual GPU acceptance.

