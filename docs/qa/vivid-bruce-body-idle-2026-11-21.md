# Bruce older character + body idle — 2026-11-21

Character: published MetaHuman companion Bruce from
https://github.com/creategamecharacters/metahuman-to-glb/tree/main/docs/5.7/characters/bruce.
Source and five original coverage maps downloaded to KINGSTON elder-skin/source and
bruce-maps. Provenance SHA256 and adaptations in source/bruce-provenance.json.
Repository LICENSE copied for provenance; no claim that its code MIT relicenses
Epic character content. Local testing only, no character binaries vendored/pushed.
This is an adapted MetaHuman companion GLB, not reconstructed .dna or a new scan.

Full model has body, three clothing primitives, hair, beard, mustache, eyebrows,
face/eyes/lashes. prepare_full_character now accepts generic FaceMesh and attaches
beard/mustache as well as hair/brows. New prepare_companion_coverage consumes the
upstream alpha_stem/channel metadata and original coverage maps, keeping decoded
alpha; source RGB factors are used with explicit external60% grey colour blend.
Final card alpha BLEND; early MASK experiment too sparse at full-body distance.
Eye shell's nonstandard Unreal mask is hidden; actual eyeball retains source iris
atlas and opts into existing layered optics. Cloth factor roughness.85, normal.35.
Facial skin/base and normal image contents preserved. Triangular collar/cloth
artifacts REMAIN visible; no complete MetaHuman Creator quality parity claimed.

Body preview:
- More visible breathing, spine sway, delayed arm/elbow/hand/shoulder settling.
- Resting poses bake small pelvis weight shift through shared solveTwoBoneIK only
  with complete direct thigh/calf/foot chains, ankle targets/orientations planted.
- Canonical spine_01/neck_01 preserved before stripping generated suffixes; common
  MakeHuman upper-body spellings mapped. A/T poses retain their target stance.
- Six-second periodic clips and existing Animator phase-preserving .65s crossfade.
- Creator default now uses shared humanPreviewClip rather than head-only bob.
- --preview-fixed-pose captures one idle; old diagnostic sequence cycles poses
  every72frames. Do NOT label bruce-planted-idle/bruce-body-idle.mp4 as pure idle.

PASS builds Creator/Studio on KINGSTON: build-weightshift.log/build-fixed-idle.log.
PASS wardrobe native test extended with moving spine/hand, periodic quaternion
endpoints, complete-leg weight shift + planted ankle assertions at5phases.
Initial test failed canonical spine suffix recognition; fixed then PASS.
PASS existing bind-preserving groom tests and morph-normal tests. Native session
appearance test PASS. No live GUI/Vulkan/mobile/CI/performance benchmark.

Captures: bruce-reviewed-close42frames portrait light, reviewed PNG0/41 open/blink;
bruce-continuous-idle144frames relaxed fixed pose/catalog light,6seconds24fps;
57headCommands,52colour/shadow, AA2passes, pipelineFailures0.
All under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/elder-skin/.
Final deliverable video: bruce-continuous-body-idle.mp4; final stills
bruce-continuous-full.png and bruce-skin-reviewed.png. Earlier baseline ignored
coverage and eye shell: visibly failed. Python parallel download first failed
local certificate verification; curl retry downloaded maps successfully.

Next: resolve cloth faceting/collar intersections and remaining shadow artifacts;
calibrate hair/iris optics against source viewer, verify motion on additional rigs.
