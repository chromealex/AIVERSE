# Older male skin QA — 2026-11-21

Result: native import/animation/capture PASS; skin visual acceptance FAIL.
No MetaHuman quality parity claimed. No screenshot retouching.

Source: [Patchbay MakeHuman old-man.glb](https://github.com/Platano78/patchbay/blob/113971b05b83ec5297138741be504774d9d0326e/webclient/avatar/model/old-man.glb).
Upstream LICENSE-NOTE.txt attributes mesh/morph/texture content to MakeHuman CC0
packs and rig to TalkingHead MIT. This is a parametric older male, NOT a scanned
elder or MetaHuman DNA asset. SHA256:
712b2eb30921332969382c9b7aa8fb19af5f61c51678519423998cf37b00d5c4.
Original and provenance retained in external elder-skin/source/; not vendored.

Reproduction (cwd this worktree; all outputs on KINGSTON):
```sh
node tools/vivid-dna-import/prepare_elder_fixture.cjs \
 /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/tooling \
 /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/elder-skin/source/old-man.glb \
 /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/VIVID-Look-Lab/content/Characters/Elder-Skin-QA-v2
```
Tooling: existing gltf-transform4.2.1/sharp0.34.5, meshoptimizer installed externally
(dependencies.log; npm reports one high advisory, no runtime/editor dependency added).
Meshopt decoded, WebP converted losslessly to PNG from decoded pixels. Speech-only
visemes removed because initial67 named targets exceeded engine64 limit. Final52
ARKit body morphs retained with matching indices/weights. Root wrapper scale2 and
eye-height framing preserve the original rig; skin maps to VIVID shader. No invented
normal/roughness textures and no Ada atlas smoothing. Source diffuse is2048x2048;
normal and roughness maps absent. Consequently not a micro-normal quality benchmark.

Native command: AIVERSE_VIVID_CREATOR_CAPTURE=<output-dir>
<build>/aiverse_vivid_creator --smoke --still --closeup --studio-lighting catalog
--preview-pose portrait --preview-sequence48 --preview-head-sweep --asset <manifest>.
Actual CLI has separate arguments `--preview-sequence 48`.
Second run dramatic/144frames. Diagnostic catalog/shadow-quality off/1frame.
Build is the restored-shader Creator from preceding native FXAA stage.

Outputs: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/elder-skin/.
- soft.log: initial import rejected67 named targets (expected limit, not PASS).
- soft-v2.log:48frames, side.log:144frames, no-shadow.log:1frame.
- AA enabled1, passes2; shadow map4096; pipelineFailures0.
- validation.txt: decoded PNGs, <=64 matching morph names, expected frame counts.
- soft-front.png, soft-turn.png, side-turn.png, side-blink.png, side-opposite.png,
  no-shadow.png: converted directly from native PPM, visually inspected.
- elder-side-motion.mp4:144frames,1500x920,24fps,6seconds; encode.log PASS.

Findings: age-related folds/color visible, but source lacks pore relief; skin looks
flat/yellow under catalog lighting. Severe triangle-shaped shadow patches on cheeks
and neck become stronger under dramatic light. At the same frontal pose, disabling
shadows removes neck triangulation, corroborating a renderer shadow issue independent
of Ada atlas. Eyebrow/lash alpha edges remain coarse. Blink frame41 closes lids;
head rotation and opposite direction frame110 are captured, no complete body pose
or cloth test on this head/bust fixture. No per-frame automated visual acceptance.

No Studio GUI, Vulkan/mobile, CI, performance or full-resolution scanned-skin test.
Next: fix shadow acne/discontinuities using this model plus Ada A/B; then test an
elder scan with authored normal/displacement/roughness maps for skin microdetail.
