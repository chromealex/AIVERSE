# Model additive blending — #219

Source commit: abeca2a1001c20fe52a617bbc2fdb03e8bf34138
Branch: codex/issue223-minimap-atlas-20261126

Shared owner: engine/render/MaterialBlend.h. AlphaMode appends Additive (SRC_ALPHA, ONE) and AdditiveUnweighted (ONE, ONE), with common classification, parsing and RHI mapping. Existing enum values preserved. RHI appends unweighted additive; GL, Metal and Vulkan source implementations updated. No shaders changed. ModelShaderKey now distinguishes pipeline blend state without duplicating shader variants. Renderer phase sorting, shadow eligibility, opaque batching and virtual fallback use common transparency classification.

Import: MDX Additive/AddAlpha retain actual additive semantics, including material alpha; glTF extras.aiverseBlend. AppearanceAuthoringService accepts blend overrides. FX model material tracks persist blend and do not overwrite additive state with alpha tracks. Modeling export preserves glTF extras; proxy bake and Studio reject additive where they reject other transparent input. Full changed paths are recorded in docs/features/model-additive-blending.json.

Evidence: git diff --check passed. Added common policy regression and FX JSON roundtrip assertion; tests and builds NOT RUN per final-build instruction. GPU blending equations, import/export fixture roundtrips, transparent sorting, cache separation, material overrides and captures still need actual verification. No #219 closure/merge/push.

Next: inspect serialization/cooked material enum validation, shader output alpha semantics for both modes, all MDX multilayer paths (existing layer approximations may remain); extend import/export and GPU regression coverage. Implement #221 with PRISMA-owned display-space composition (no duplicate full-screen owner), then execute final build and all applicable gates. Other stages remain unverified; #206 requires native Windows evidence.

## Import layer audit — 06817a3e3f44ad3b60bed4099df0d83cdea2b226

Found extra additive MDX/MDL layers were dropped by the original first-layer conversion. Now additional Additive/AddAlpha layers become material passes sharing the same primitive indices/vertices/palette. HD texture channel layers retain their existing mapping. Team-glow uses its authored filter, and an additive main layer over team colour retains its blending. Added synthetic MDL regression for extra AddAlpha with 0.5 alpha and shared geometry. git diff --check passed; no build/tests run.

Native compile and GPU acceptance remain pending. Next start final verification only after confirming main synchronization and reviewing all required test targets. Current model mode and FX space changes are not accepted merely from source inspection.

## Authoring completion stage

Current runtime Metal evidence passes, but material_set and native Studio validation still reject additive values. Complete both through render::parseMaterialBlend and expose the two modes in the existing Studio material select. Add native and frontend regressions. Full Mac gate passed at d88e86901 with Studio disabled; Studio itself remains to build and verify.

## Material effects example plan — 2026-11-27

Prepare one ordinary data-only demo for shadows, both additive model modes and linear/display emitters, reusing the verified shadow fixture. Existing owner APIs only. Native/Web acceptance and publication pending.
