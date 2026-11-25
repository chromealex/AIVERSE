# Animated model effects (#144)

Acceptance covers a model-only effect, mixed emitters, looping/one-shot clips, attachment movement and teardown, independent colours/alpha, image-frame swap and UV motion. Runtime tests deferred.
f89cf9c82d765a89097061d9cf9d21a92ecd7d29
FxEffect parses and writes model source, clip, lifetime, material gradients, UV speed and image frames; ParticleSystem retains model-only and mixed instances and publishes presentation poses by existing generation-safe handles. Rendering stage follows.
a2b523d817aa450381b5be1898924cc2b06f9b42
AnimatedModelEffectView uses ModelAssetProvider, bindPose/sampleClip/worldMatrices and Renderer3D ModelDraw. The source material is copied per instance for RGBA gradient, image-frame and UV tracks. ClientFx and editor preview share it; native and graphics checks pending.
d752ccdff642b1306e7ef929dd90118bde92e976
Added fx JSON example and pending regression for model-only schema round trip, instance motion, stop and one-shot expiration. Skinning, material tracks and asset appearance will be checked on the combined tree.
b2f2b82566f5f3f49c6b69604e04648836c00481
Mac syntax check of tests/tests.cpp exposed the JSON round-trip parser argument mismatch; fixed its use of the existing C-string test helper. Verification pending.

0e4a946351b20dca76bdad2b37851832ecd7318f. On Metal, two model-only effects followed their scout anchors and advanced a translation clip, UV scrolling and opacity, but both phases retained the first magenta texture. The glTF loader retained the internal sampler image names and discarded the authored texture and image names. Model.h, Model.cpp and ModelGltf.cpp now retain aliases independently of those internal names; AnimatedModelEffectView resolves each frame using Model::findImage. Includes the URI and shared-image/two-samplers regression in tests/tests.cpp and authoring notes in docs/particles.md. Before-fix frames and fixture: issue-backlog-demo/qa/animated-effect-{110,240}.png and animated-effect-acceptance on KINGSTON. git diff --check passed. The revised binary and its two-phase captures are pending.

The revised aiverse_models built on Mac. The six assertions from models_gltf_resolves_authored_texture_frames passed against the rebuilt x86_64 library, including independent selection of two texture samplers using the same external image and compatibility with its existing internal name. The initial standalone link omitted Zstandard, and the first shell retry selected arm64; linking x86_64 with the project's Zstandard archive succeeded. Native frame comparison still pending.
