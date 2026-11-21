# VIVID subsystem agent rules

Read `AGENTS.md`, `docs/vivid.md` and `docs/agent-handoffs/vivid.md` **before each stage**.
Current feature branch: `feature/vivid-digital-humans`. Other agents edit models, shaders, batching, animation and Prisma; coordinate shared files, do not overwrite their work, and sync main at integration boundaries.

## Ownership and contract
- Own `engine/character/vivid/`, `tests/vivid/`, `docs/vivid.md`, `docs/agent-handoffs/vivid.md`.
- Core facial module must remain C++20, native, portable and independent of rendering, UI and genre. Public API `aiverse::vivid`, linked as `AIVERSE::vivid`.
- Face channels use the fixed, indexed 52 ARKit-style names in `Face.h`; changing order is an asset/network compatibility concern. Do not silently renumber.
- Layers Tracking -> Emotion -> Speech are masked and blended in order; animation sampling explicitly calls `advance(dt)` or `snap()`.
- All numeric external inputs must reject NaN/Inf, saturate normalised controls, and avoid unintended state changes on errors.
- No authoritative ECS gameplay mutations, no Unreal runtime dependency, no bundled third-party human scans or MetaHuman assets.
- Later render mapping must preserve existing AnimationGraph/Model semantics and respect `kMaxMorphTargets=8` until a separately approved extended deformer lands.

## Every-stage protocol
1. Implement one bounded stage and commit changes. Keep public API and compatibility documented.
2. Run scoped tests when a build host is available; only describe tests actually run.
3. Add a **separate** handoff commit on this same branch with actual implementation SHAs, tests, warnings, unfinished parts and the next action.
4. If no build is available, mark full native tests NOT RUN. Pure source checks are not a substitute.
5. Update `docs/vivid.md` status when advancing beyond the design stage.

## Digital-human authoring (Stage 6+)

- `HumanDNA.h/.cpp`: open versioned recipe. Validate finite values, rig
  hierarchy, guide limits and 52 channel order before any GPU publication.
- `HumanRig` composes facial joint matrices on a COPY of MotionWeave pose;
  never mutate MotionWeave ECS state or run a second animation scheduler.
- `HumanMaterials`: opt-in Skin/Eye/Hair shader graphs and paired GLSL/MSL
  PBR lobe changes through normal `render::Model`; preserve imported albedo,
  normal, roughness, AO, alpha-cutout and UV transforms. Recheck material GPU
  equality/hash when introducing new parameters.
- `HumanGroom`: deterministic bounded scalp guide/cards; dynamic vertex
  stream and quality tiers. Do not merge dynamic hair cards into pooled geometry.
- `HumanImport`: user-authored glTF/FBX/OBJ + `HumanDNA` + optional sparse
  morph JSON. Validate relative paths, source model-relative vertex IDs, and
  imported rig names. Never deserialize proprietary MetaHuman DNA as though
  it were the VIVID v1 schema.
- `HumanCreatorSession` and `tools/vivid-creator`: a standalone ImGui
  character authoring UI with validated undo/redo, safe JSON persistence,
  and explicit imported-asset modes. Do not destroy GPU actor buffers inside
  a pending GPU draw. MotionWeave owns body/node animation.
- One-shot Mac acceptance: `bash tools/vivid-validate.sh` after fetching the
  branch; includes 16 CTests and four real GUI modes (1/12 actors, procedural
  Creator, imported OBJ Creator). The full AIVERSE regression is
  `bash tools/vivid-validate.sh --full` when merge readiness is required.
  **Never claim new stages Passed until actual host logs exist.**
- After each stage append source SHAs, actually run tests, known limitations
  and next steps to `docs/agent-handoffs/vivid.md`, in a separate handoff
  commit. Realistic eye/skin/hair quality requires licensed source human
  assets and artist validation; procedural references only test the pipeline.
