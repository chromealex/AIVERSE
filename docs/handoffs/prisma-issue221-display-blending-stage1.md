# Display-space additive particles — stage 1

Source SHA: a91527be52fdc112f7c6f273696e5759ca41a109
Branch: codex/issue223-minimap-atlas-20261126

Renderer3D.addDisplayDraw queues an output-pass callback after the existing PRISMA resolve. Original scene depth is borrowed read-only; output size is passed for normalized depth sampling. Registration returns false if there is no active postprocessor/depth. Callbacks clear at begin/reset/shutdown and move out before resolve, preventing accidental replay. No added tone mapping/compositor owner.

FxBlendSpace Linear/Display is authored per emitter and trail, with trail inheritance and explicit override; only additive display blending is accepted. Serialization and trail equivalence preserve the setting. Tests add emitter/trail roundtrip and inheritance cases.

Changed paths: engine/render/Renderer3D.{h,cpp}, engine/fx/FxEffect.{h,cpp}, tests/tests.cpp, docs/prisma.md, docs/spectrafx.md, manifest, roadmap/policy. git diff --check passed; compilation/tests/GPU NOT RUN per final-build request. Data is NOT connected to rendering yet; do not ship or close #221 from this stage.

Next: extend CPU batch key with blendSpace, queue display geometry without drawing it into HDR, implement GLSL/MSL display draw shaders with original depth occlusion and fog, and register renderDisplay via addDisplayDraw from all hosts (client/editor/LENS). Preserve default and LDR behavior; ensure GPU-owned emitters support the same request or route explicitly to CPU at admission, not double simulate. Audit shader-graph particles, output scale/flip and depth lifetime through PRISMA. Add actual GPU pixel/capture regression before final acceptance.
