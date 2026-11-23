# Handoff: building-style geometry grammar stage 2 — 2026-11-23

## Scope and provenance

Parent feature: `feature/aiv20261122_building_styles` @ `ffef5ce0a1af42332a8eb8b47bb222d3eb07208e`.
Working branch: `feature/aiv20261123_building-grammar`. Implementation commit: `853a51ab9203543310e5b5a0f4312a2b37cb8d43`.
Do not merge directly into main: parent branch predates many main commits and should be
rebased/merged by the integration owner after checking other worktrees.

## Implemented

- `BuildingStyle.h`: geometric `StructureMassing` enum (Bastion, Spire, TwinBay, Terraced)
  parsed/validated from `structures.<kind>.massing`. No genre-specific switches.
- `BuildingStructure.h`: optional `StructureSize{width,height,depth}` added as the sixth
  argument of `dressStructure`, preserving every five-argument call. Full envelope fit
  after genuinely recomposing each silhouette at the requested proportions. Normal
  vectors transformed by inverse scale; collision boxes/gate anchors transformed too.
- `scifi.json`: barracks=bastion, command=spire, factory=twin-bay, depot=terraced.
- `aiverse_building_export`: `--size W H D` with `--structure KIND`.
- Tests extended to check grammar parsing, four yaw orientations, exact envelope,
  deterministic collision and invalid requests.
- Updated `docs/building-style.md` and `aiverse-building-style` skill.

## Tests actually performed

- Source-level unique-anchor and modified-file presence checks passed during GitHub write.
- **CMake/C++ compilation, CTest, modelviewer, screenshots and GPU benchmarks NOT RUN**
  in this ChatGPT environment. The previous agent's Linux tests only covered stage 1,
  not these changes. Do not mark this feature verified or advertise new screenshots.

## Reproduction commands (from the worktree)

```sh
cmake -S . -B build/building-grammar -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/building-grammar --target aiverse_library_building_style_tests aiverse_library_building_tests aiverse_building_export -j 4
ctest --test-dir build/building-grammar -R '^library_building_(style|gen)$' --output-on-failure
build/building-grammar/aiverse_building_export --style engine/assets/building-styles/scifi.json --structure factory --size 24 12 18 --seed 482733 --out /tmp/factory.glb
```

Determine actual binary path via `find build/building-grammar -name aiverse_building_export`
if the CMake generator uses a subdirectory. Capture modelviewer images for seeds 1/7/42
for all four functions, check tall/narrow extremes, inverted face normals, zero-thickness
parts, gate access and generated bounds. Run full CTest and Metal/GL before integration.

## Known limitations and next work

1. The original `Setting` and `Style` are still one `BuildingStyle` JSON; extract distinct
   setting/style/role registries and resolve presentation+gameplay rule ownership.
2. The final envelope fit can scale detail thickness/door proportions at extreme ratios;
   add constrained grammar layout that maintains fixed real-world clearances.
3. Non-enterable sci-fi structures still have **closed AABB hulls**, even where visual
   gate leaves are drawn. Do not claim true enterability.
4. Build the HTML5 Studio window via the approved UI kit and bridge, with preview seeds,
   asset GUID/meta + undo/redo. Do not write project files directly from browser JS.
5. Build runtime `StyledMesh` → `render::Model` bridge, resource lifecycle, true PBR
   trim textures, multiresolution LOD, GPU and mobile validation.
6. Publish a devlog only after a real in-engine screenshot + live verified web demo
   where required. Existing devlog remains `in-progress` and is not proof of stage 2.
7. Before merging, run on updated main and resolve concurrent tool/roadmap changes.

Release notes suggestion for 3.1.0 **once verified and merged**: "Parametric structure
dimensions and silhouettes for procedural building assets". Breaking changes: no
existing API calls or serialization formats changed; explicit size inputs are opt-in.
