# PHYSIS Stage 23 — transactional runtime field reconciliation

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
Implementation SHA: 001b5f447f10ea0da605ba6bc2fd23802282df5f
PR: #25 (DRAFT)

## Implemented
New engine/physis/PhysisRuntimeFields.h exposes RuntimeFieldBinding, RuntimeFieldRegistry, validateRuntimeBindings, replaceRuntimeField, reconcileRuntimeFields. The latter accepts fully validated desired AuthoredField values, sorts by stable ID, stages a World copy, destroys removed/type-changed entities, retains entity identity on same-ID/same-kind updates, spawns additions and publishes by move/swap. No genre, editor, renderer, water or Box3D dependencies. Caller must invoke only at a simulation barrier, not during ECS iteration.

## Important limitations
The runtime ID registry is currently a HOST-OWNED value, not an ECS persistent resource. It must be saved/restored together with World for rollback and replay; otherwise do NOT use this API in rollback-enabled production games. The initial installFromMap/spawnFieldAssets path does not populate this registry. A host must start with an empty registry and call reconcileRuntimeFields on an empty authored-field World, or implement a verified registry-aware initial spawn. Calling it with an empty registry against a World already containing authored field entities would duplicate fields. Runtime hot reload is NOT wired to MapDoc or editor. The function does not validate arbitrary manually constructed component payloads: call parseFieldAssets first. Structural World publication invalidates prior pointers/references.

## Verification (actual)
GitHub connector file write succeeded. C++ build NOT RUN, CTest NOT RUN, editor GUI NOT RUN, save/rollback parity NOT RUN. No performance measurements. Do not merge the draft PR.

## Next agent
1. Inspect compiler behavior of World::add on existing component and World move assignment; fix issues and add registered CTest covering unchanged/updated/replaced/removed IDs, invalid registry, rollback and exception safety.
2. Move RuntimeFieldRegistry into opt-in persistent ECS resource, implement Archive io, and register with Simulation only under an explicit feature flag. Reconcile initial installFromMap so field identities exist from tick zero.
3. Add an authoritative tick-boundary command queue, host-driven editor apply, network replay and save migration policy. Never mutate live World from ImGui callbacks.
4. Integrate other PHYSIS subsystems via read-only sampleNatureField adapters without duplicating NEREID/TerraWeave ownership.
5. Update root AGENTS.md and skill after validating runtime integration; retain separate stage handoffs.

Tests delegated to separate testing agent; no PASS claim.
