# Caverns Stage54u1 — persistent planetary gameplay authority

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `fef8cdfa4b3b100b5b3d034b48fc8f79c4c19eb5`. Main unchanged; compare before work reported zero commits behind main. Roadmap e21c3b6b.

## Source commits
- 41de193a: `engine/world/TerraWeaveCavernsEcsAuthority.h` extracts the existing detached stamp transaction kernel `planAuthorizedCaveStampCommand`; flat wrapper retains its existing resource and optional planetary anchor validation.
- ee0daafd: `TerraWeaveCavernsPlanetRegionSet.h` defines one persistent CPU-only CavePlanetAuthoritySet; canonical sorted stable region IDs, independent volume revisions and stamp allocators; shared fixed-point overlap policy against actual built Surface; v1/v2 authored JSON load; bounded aggregate records; binary archive and immutable recipe validation on staged load. Editor selection/next editor ID intentionally excluded from gameplay hash. Native freeze and authorized mutation helpers.
- 02aea2e2: `engine/game/GameCore.cpp` installs region authority from standard spherical GameCore setup after actual Surface creation, before persistent registry seal. Other specialized host systems that bypass this path must explicitly opt in with their own actual Surface; not a claim of automatic Actor/Voxel support.
- 250bb7c3: `TerraWeaveCavernsEcsCommand.h` adds region-addressed native command adapter: two uint32 region ID words followed by existing ten stamp words. Owning game must reserve command type and supply authorization; no unrestricted default dig command installed.
- 2facd101 / 2237e43c: `tests/terraweave_caverns_planet_authority_test.cpp` + root CMake CTest registration. Native GameModule fixture exercises v1 decode, v2 overlap rejection, canonical order, selection-independent hash, edit isolation, stale revision refusal, save restore, rollback. Not executed.
- 0c418092: editor region helpers reuse region ID/overlap policy, validate malformed overlapping v2 documents and aggregate budget before writes. Existing Studio authoring and document schema retained.
- fef8cdfa: Caverns skill updated.

## Contract / scope
This is 54u1, not the completion of 54u. All loaded regions are authoritative authored SDF inputs in ECS, not resident meshes. PersistentEcs owns save/hash/rollback; the immutable recipe validator fences anchor, ID, generator settings and entrances. Stamp changes go through the same validated CPU transaction kernel and report dirty bounds. No new genre-specific physics or renderer, no automatic gameplay surface holes, collision, navigation or chunk activation. Native command route is opt-in and not a gameplay default.

## Verification
GitHub branch exact code/test HEAD confirmed; workflow lookup returned zero runs. Native compile, CTest, frontend, performance and hardware acceptance NOT RUN. Existing earlier handoffs also record unverified stages; do not treat these as green. KINGSTON merged-tree gate remains mandatory.

## Next Stage54u2
Inspect PlanetRenderer's single-body cut lease and native CavePlanetNativeOwner stage/commit path. Implement a body-level aggregate cut transaction across disjoint chart leaf sets with one revision/epoch, explicit original surface retirement, shared slot/triangle/GPU/physics budget, no duplicate baseline collision ownership, and atomic Box3D + SpatialWorld + geometry publication. Preserve rollback/retirement and reject conflicts before mutation. Then connect loaded ECS regions to runtime chunk demand, immutable queries and publication, add real tests; PHYSIS gravity-aware navigation is 54v. No merge to main yet.
