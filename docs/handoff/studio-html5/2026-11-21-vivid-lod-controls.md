# VIVID LOD preview controls

## 2026-11-21 — character LOD assembly + Studio controls

Implementation `95bae785b3893ca75f4d5135165f6478254df97f`, branch
`integration/motionweave-studio-facial-stage184`. Wardrobe keeps/remaps reachable
authored LOD chains for base and added parts; body/face deformation includes
alternatives. Native Studio existing animation bridge accepts optional validated
automaticLod (defaulttrue for old calls). Shared UI offers auto/LOD0, native facial
renderer consumes it. Parts keep individual thresholds; no equal-level controller.
Creator logs actual level counts; GLB chain tool registers standard extensions.

PASS Creator/Studio KINGSTON builds; CTest4/4 auto_lod_runtime, vivid_human_wardrobe,
vivid_face_shape, studio_human_appearance_session. Initial session test compile
failed Json::number(3) ambiguous, fixed3.; no remaining build failure. Standalone
ModelLodTransition assertions built with -UNDEBUG and passed (configured target
was absent). Typecheck/web tests/build PASS; Python UI29/29 PASS. Native elder
auto48frames LOD1=5 vs highest1frame LOD0=6; full Ada auto48frames LOD1=5,chains6,
blocked0,pipelineFailures0. Selected PNG inspected; video48frames24fps encoded.
Fixtures use diagnostic index simplification, retain original vertex/morph arrays,
threshold5000 deliberately exercises coarse levels. No VRAM/performance claim.
See docs/qa/vivid-lod-completion-2026-11-21.md for exact logs/limits and paths.

No GUI/live zoom-boundary/Vulkan/mobile/CI/push. Previous authorization: verify
without interface. No production source asset replacement. Shadow artifacts
remain unresolved. Next: production-authored LOD threshold tuning and live zoom
visual QA, followed by separate renderer-shadow investigation. No save/ECS change.
