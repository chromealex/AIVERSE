# Native viewport FXAA

## 2026-11-21 — native FXAA and neck albedo diagnostic checkpoint

Implementation: `3d267f847a5a6500884418f9d34ec78d61ca9237`; branch
`integration/motionweave-studio-facial-stage184`. Native Studio/Creator now
resolve LDR FXAA through existing PostFx; retirement/shutdown included. Creator
--no-aa supports comparison. No temporal AA, save format or ECS changes.
Opt-in importer neck_albedo_smoothing filters only known lower MetaHuman atlas;
face/normal maps retained. External Ada source updated with backup on KINGSTON.

PASS Creator+Studio builds (build-restored.log), filter test, existing baker
test, optional real-import comparison; native168frame video and source reload
AA enabled=1 passes=2 pipelineFailures=0. Exact artifacts/commands and limitations:
docs/qa/vivid-neck-aa-2026-11-21.md. PostFx CTest discovery found zero tests.
No GUI, Vulkan/mobile, performance, CI or push. Skin texture cleanup is cosmetic.
Residual hard neck shadow at blink frame41 UNRESOLVED; unoffset receiver-plane
derivative experiment failed visually and was reverted (background seams).
Next: test user-requested elderly wrinkled model, then isolate neck shadow edge.
All builds/media: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/.
