# Shared VFX host integration

## Stage 4 — d814d995e2fe82f636450470d5b819a8c3678c83

Renderer pointer now propagates through BatchWorld, GpuPoolWorld CPU fallback, GraphRuntime, SceneVolumeWorld and SceneVolumetricParticles inline draw adapters. ClientFx passes its renderer into pool/volume draws. StudioSpectraSources/Preview, Studio scene/dock, Editor GPU preview and native actor frontend use the same interface. Existing GPU-only volume requirements remain explicit incompatibilities for unsupported display requests. GPU admission regression added to tests/vfx_graph_runtime_test.cpp. Avoided registering callbacks when no display batches exist. Verified by source inspection that endPresentationFrame only clears frame flags and leaves CPU upload alive through resolve.

Changed paths are listed in the feature manifest. Documented authoring, native lifecycle, postprocess ordering and current GPU requirements in docs/particles.md; PRISMA/SpectraFX design status updated. git diff --check passed; tests/builds/GPU captures NOT RUN. No issues closed, no push/merge.

Next: full source audit of #219/#221 before final build, especially material serialization/MDX multilayer semantics, shader template injection and frame source-depth lifetime. Add synthetic GPU probes for additive model pixel equations and particle display brightness/occlusion/graph parity; then final cached KINGSTON build and merged-tree quick gate, followed by feature-ready checks and real evidence. All other issue fixes remain unverified; Windows #206 still needs actual runner evidence.
