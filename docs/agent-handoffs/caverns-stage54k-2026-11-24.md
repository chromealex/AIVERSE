# Caverns Stage54k — paired native rim preview and shared driver

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- b9c4d8707240e64fd6a765ab827625c5036e7fad: exact bounded visual residency demand and generation invalidation on same source.
- 1e50e054ea415763a76c4f8c5f093c2f82090445: existing client visual owner gathers and stages the complete constrained replacement group in existing GeometryScene shards; no extra renderer.
- eae22f37e534fb48301935abf49dbf95685abf7a: sparse preview prepare/submit separation and shared plan.
- 131a0336899bafc5a108da0a4fcd27d53bb72316: Studio exact plan coordination, automatic constrained trim-plane choice and solid fallback.
- bc2da02d69bc30ffa52f6f509d805efe44c46c44 / 51d9394102d552ad3cf295dbbb3c4f86d7d8a037: paired frame submission and exact sparse origin.
- ad38f429e3ed6f22d34080185c5f0ed0d9a6542d / 51033ab1217aaad4833c63e572d89a27eb5d8b55: actual terrain edge and triangle refinement at volume grid crossings before building collar.
- 2787d90d658b9d35fea1e50062c1d97e3daecf3a / efb81cd769eb497fe225d4bef60e90b9b3dffb5b: typed Studio diagnostics.
- 28f4def34fceba65b704758a957d5c81503cd64d / 5d78f712835fa81046c2b8911fde5d6b714a185a: document content invalidation and committed catalog slot readiness proof.
- 579e026d23a4576b20b06a6114c40e7a7e46f58f / c5ff4eecb7b19c224ce0a76cec844080acf8e6cc / 4a9b70e229b494d25381d9521b4984c9ac883616: shared refined native authority entry and opt-in existing TerraWeave native driver path.
- f9304b2d787a3891882ba630f2648960e83533ab / 557791514a61dec73c49e4abfff8921efa53d5cc: skills.

## Contract and limitations

Use the same refined immutable surface group for both terrain and volume. Grid refinement subdivides actual terrain rim triangle edges on exact Q16 grid planes; ambiguous quantization, invalid fragments or mismatched face sets reject the plan. It is not a general noncollinear seam repair or cross-platform deterministic certificate. Existing constrained star-shaped, isolated shaft and rectangular owner limits still apply. Studio uses existing visual owner and worker scheduler; explicit group replaces camera demand (up to 64 volume slots). Paired sparse surface and exact volume source draw only when both owners ready. Refusal drops cut group and rebuilds solid surface. Temporary blank frames, conservative rejection and a failed normal-stream recovery remain possible. No duplicate collar overlay; legacy heightfield and arbitrary entity asset preview are not paired with the collar. Studio is visual only, not a gameplay collider owner. The new native driver path requires explicit invocation by the actual host, frozen ECS/soil, real reader exclusion, reserved IDs, stable revisions and real Box3D/GI/exact navigation readiness proof. No automatic GameModule registration, no universal rollback, no exact SDF navigation projection. Native compile, CTest, frontend build, GPU/physics or visual tests NOT RUN.

## Next Stage54l

Start planetary adaptation using existing planet coordinate/terrain and PHYSIS gravity APIs: establish immutable local frame, stable signed local centimetre coordinates, conversions and constraints for surface/volume mesh and physics/nav owners. Do not use global Y or flat Earthworks surface as planetary terrain. Continue concrete game-host integration/recovery and QA after feature work. Mandatory KINGSTON native gate before main.
