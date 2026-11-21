# VIVID Stage 7AI — source inventory and Texture RHI merge-risk gates

Date: 2026-11-20. Branch: `feature/vivid-digital-humans`.
The audit is of **committed source**, not a claim of compiled, visually validated
or production-ready character editing. `main` and the texture agent's
`feature/texture-rhi-unification` are not to be modified by this stage.

## Source-implemented Creator authoring surfaces

Verified in `tools/vivid-creator/main.cpp` via actual ImGui control paths
and associated core VIVID headers; no stand-in UI or hypothetical tools:

| System | Current source-level functionality | Limit before production |
|---|---|---|
| DNA/face/body | Presets, editable procedural proportions, eyes, skin, 52 facial defaults, undo/redo, versioned JSON | Imported mesh proportions are not resculpted by procedural sliders |
| Asset ingestion | glTF/FBX/OBJ+manifest, material upgrades, rig and morph sidecars, imported/procedural switching | Needs licensed 3D scan acceptance fixtures, texture/LOD and rig topology validation |
| Rig authoring | Bone alias mapping, translation corrections, rebind, rig JSON undo/redo | Not a complete DCC skeleton authoring/weight-solve workflow |
| Skin weights | Mesh/joint choice, named bone weights, screen-space heatmap, click/drag brush, mirror X, transactional history, LOD transfer, seam analysis/split subset | Picking is CPU reconstructed skin; not necessarily bit-exact to all GPU morph/cloth, ambiguity fails closed |
| Facial morphs | Import sparse named targets, brush and mirrored source vertex edits, per-channel routes/gains, JSON export, authored LOD mapping and GPU publication | Not full topology-aware/geodesic sculpt, production deform cage or GPU-accurate selection |
| Hair | Cards, groom quality, fixed-step Verlet, point/segment collision, scalp BVH+refit, wind and edit controls | CPU cards and bounded optional narrowphase are not strand-rendered production hair or swept CCD |
| Skin/eye/hair visuals | Existing Skin/Eye/Hair shader models, optical DNA cases, low/medium/high graph tiers, Renderer3D presentation bridge | Approximate optics only; real device GPU timings and visual acceptance outstanding |
| GPU lifecycle | Candidate retirement queue and renderer model pipeline retirement | Must confirm same-fence texture retention after Texture RHI merge |
| Project appearance | Presentation JSON and game-host quality adapter | Not a global automatic quality controller for all games; hosts opt in |

## Prioritized unclosed functional work

1. **Character geometry/DCC:** general imported mesh shape sculpt, sculpt brushes
   with geodesic/topological falloff, robust per-character symmetry plane,
   full authored facial/body rig editor and validated high-quality LOD retarget.
   This is larger work than adding more DNA sliders.
2. **Selection and deformation parity:** object-ID/depth-assisted GPU picking,
   exact skinned+morphed viewport vertex correspondence, constraints under
   cloth and dense triangle meshes; direct pixel/delta parity on Metal/GLES/
   Vulkan against actual output.
3. **Production hair:** hair groom asset ingestion, quality-specific card-to-
   strand migration, GPU simulation/collision option, throughput and memory
   budgets on target phones and desktop GPUs.
4. **Material optics and assets:** thickness-aware skin diffusion (not just
   warm screen/surface tint), realistic cornea/refraction/tear line and more
   physically grounded hair transport; artist-ready scanned assets and
   lighting reference captures.
5. **Texture RHI integration:** compressed maps, mip residency, sampler
   management, material identity, alpha-correct hair and GPU retirement. VIVID
   must *not* own parallel RHI staging or enable VT on ineligible material
   graphs/cornea; follow the existing
   `docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md`.
6. **Final native acceptance:** full C++ compile, VIVID/engine CTest, creator
   capture A/B, hardware GPU parity/cost and error remediation. Tests added
   to source are not test results.

## Cross-branch overlap tool

Script `tools/vivid-creator/check_texture_rhi_overlap.py` (Stage 7AI
implementation SHA `01304d0a55782eb0fe3c8a1a694b7b3faae0f5fb`) uses
`git rev-parse --verify`, `git merge-base` and **two separate**
`git diff --name-only -z BASE HEAD --` comparisons. It reports the exact
commit SHA for both refs and their shared base, VIVID-only paths, texture-only
paths, shared paths and critical renderer/CMake ABI overlap in JSON. It does
**not** run `git merge`, `git checkout`, `git fetch`, create refs, infer
success from a clean diff, or modify any files by default. `--output` is an
explicit opt-in output-file write; `--fail-on-overlap` makes a smoke/CI gate.
A shared path is **not** proof of a true merge conflict, and the absence of
path overlap does **not** establish ABI or semantic compatibility.

Example on an already-fetched local checkout, working in any branch:
```sh
git fetch origin feature/vivid-digital-humans feature/texture-rhi-unification
python3 tools/vivid-creator/check_texture_rhi_overlap.py \
  --vivid origin/feature/vivid-digital-humans \
  --texture origin/feature/texture-rhi-unification \
  --output /tmp/vivid-texture-merge-risk.json
```

The source-only unit fixture `tools/vivid-creator/test_texture_rhi_overlap.py`
(implementation SHA `80e689feb833e1543db102c874010fe6daeac882`)
constructs its own temporary Git history with independent branches and a
shared changed path, checks actual merge-base, overlap and read-only behavior,
then inspects deterministic path categorization. It does **not** touch the
actual AIVERSE branches. Source integration into the deferred final one-pass
suite is commit `bfa6ea641a40103d09dbc60302fcdf14bd2d0b4f`.

## Known divergent surfaces and acceptance plan

Source searches / GitHub compare results show both feature branches change
`CMakeLists.txt` and central renderer/model code. Known particularly risky
paths: `engine/render/Model.h/.cpp`, `ModelShaders.h`,
`Renderer3D.h/.cpp`, `Renderer3DModels.cpp`,
`GpuMaterialState.h`, and `engine/render/graph/ShaderGraph.h/.cpp`.
Exact complete overlaps remain **unmeasured by the local script** until its
execution against fetched branch SHAs; the remote compare API can truncate
very large diffs and must not be mistaken for a complete inventory.

The texture branch has an unresolved user-reported full-engine
`aiverse_tests` SegFault and unverified later Metal shader fixes. These
do **not** establish that any VIVID implementation causes the crash.
Keep the peer branch unchanged until its owner certifies a stable API.

## Evidence and next action

- Remote GitHub file reads and source creation/update: executed.
- Python unit tests of the overlap script: **NOT RUN**.
- Actual overlap script against local Git refs: **NOT RUN**.
- macOS C++ / CMake / CTest: **NOT RUN**.
- Metal, Vulkan, GLES and Creator GPU capture: **NOT RUN**.
- These source changes are not merge certification.

Next bounded stage **7AJ**: resolve the remaining highest-priority authoring
and GPU selection gaps after identifying the smallest risk/benefit slice, or
prepare a dedicated integration branch when Texture RHI has a fixed API and
native acceptance evidence. Do not merge to `main` without actual
build/test results and a separate chronological handoff.
