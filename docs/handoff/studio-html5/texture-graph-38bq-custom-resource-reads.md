# Stage 38BQ — typed custom resource reads

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `a60ea27dcd55711f269b9cef7f653a96bcbb89d1`. No main merge.

- Extend existing validated RPN custom compute with typed read-only scalar/color graph input sampling at computed UV coordinates. Nearest/bilinear and clamp/repeat, CPU and emitted MSL/GLSL. Bounded resource index math, fixed port/channel allowlist and instruction-weighted work budget. Existing three read-only materialized buffer slots and output slot reused.
- Sample expressions lower to named temporaries, avoiding repeated dynamic coordinate expressions; resource-reading nodes are explicit fusion boundaries. Studio inspector exposes sampling settings, tokens and mirror-input example.
- Authored strict GPU and CPU expected-value cases for mirrored gradient, bilinear wrap seam, RGB channels and rejection of scalar RGB channel token. No compilation, execution, GUI or hardware verification performed. This is the safe typed graph field subset of Custom Shader Resource ABI: arbitrary native shader source, independent buffer/texture declarations, reflection, writable resources and hardware sampling remain open.
- Remaining: full HDR pipeline, ASTC/BC6H/Basis, global dependency scheduler, full cubemap PMREM, general custom shader resources and final consolidated QA.
