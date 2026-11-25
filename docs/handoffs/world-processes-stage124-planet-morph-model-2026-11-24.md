# Stage 124 — planetary MORPH model stream binding
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base 52f7c7295440dcae38a5f3ee0f8f67e24129e40e. Plan 664e61f030204fb9e16e4d62c7bd83305a873258. Implementation 425178535ddeca3d044ec6f8aaf99f8f2b03b00c; hardening ef4a1a46272154b68c525906fa298f7c2cf44abf.

## Delivered
- libraries/include/aiverselib/world/MorphPlanetModelFrame.h: explicit canonical body, existing WorldTransform rigid unit-scale affine, common frame-owner and units-per-metre GravityBinding, current completed node tick validation. Reject skew, nonunit scale, stale or different-owner inputs rather than guessing model basis. No new persistent state.
- examples/world/world_processes_native/MorphModelStream.h: strict updatePlanet and attachPlanet over existing real per-instance ModelVertex GPU stream. Identity mesh bind transforms, no skin, dedicated dynamic Metal geometry required; validates model units against native frame binding and all weighted nodes. Attachment validates actual ModelDraw transform against canonical frame with Fixed body-anchor subtraction and optional render translation; disables independent LOD and static packet revision, rejects competing pose/static paths. Existing Renderer3DModels.cpp currently admits custom vertex overrides only on Metal; no Vulkan/GL claim. Once selected, planetary mode cannot bypass validation via legacy update/attach. Failed update/rollback invalidates publication.
- examples/world/world_processes_native/PhysicalSceneHost.h: planet-specific update/attach facade, legacy bulk update skips planetary streams; rollback invalidates existing stream publication. Caller still owns asset and buffer lifetime, supplies actual correspondence and selects model instance.
- tests/world_processes_gamecore_native_test.cpp: source regression for canonical body and owner frame, shared scale, exact tick, mismatched owner/units and shear/scale rejection.

## Owner use
Provide dedicated rigid source model with identity mesh bind transforms and explicit existing WorldTransform (including matching affine) in simulation space; use the existing GravityBinding on nodes with the same owner and unitsPerMetre, even when impulseMilli is zero. Set metresPerUnit consistently. After completed simulation tick, updatePlanetMorphVisual then attachPlanetMorphVisual to the actual matching ModelDraw on its GPU frame; bodyAnchor must be canonical, renderTranslation is optional presentation-only. Existing Box3D contact bridge projects directions into the owner frame, and PHYSIS remains the gravity source. No automatic asset correspondence or runtime model/collider wiring is introduced. Legacy models and existing generic update remain available separately. A failed strict update must not be drawn through the legacy attach path.

## Remaining
Actual renderer vertex stream currently has a Metal-only dynamic gate; cross-RHI admission and validation require renderer owner work. No skin/nonidentity mesh-local bind transform or scaled hierarchy support on strict planet path. No rigid collider deformation; Box3D contact bridge and proxy stay unchanged. No automatic model selection/registration or native game activation, which remain Stage126 host integration. No second renderer, physics or ECS authority.

## Evidence
GitHub source readback and branch writes performed; actual renderer gate inspected. Native compile, CTest, real Metal draw/vertex readback, physical contact, browser/MCP/Studio, performance/platforms, captures and merged-tree Mac quick NOT RUN (owner deferred). No new persistence/schema/target or publishing acceptance. Skills and compatibility updated. No main sync/merge.

## Next
Stage 125: inspect existing SurfaceGravitySupport and GameCore/Box3D locomotion owner; implement missing native movement integration without a duplicate movement solver. Then Stage126 host activation and owner dependencies, followed by consolidated QA.
