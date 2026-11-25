# Stage 38BW — HDR alpha and linear emissive material binding

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `5d639f376373974fd208862a35fcdb48982a0888`. No main merge.

- Preserve straight alpha in radiance fields, HDR imports/constants, explicit extract/set, arithmetic (max/product/interpolate), exposure, raw readback, EXR and GPU resident half/float pack. Normalized previews remain opaque. CPU field storage is now float4 (previously float3); existing pixel-node cap bounds allocation.
- Explicit resident HDR emissive publication into built-in PBR appearance snapshots via existing transaction/rollback/pin/budget path. Tagged image provenance verifies float format, changes U2 bit and batching hash/equivalence; Metal/GLSL skip sRGB decode for linear images while legacy flags are masked. Explicit apply defaults black emissive tint to white. Studio actions for sample and scene model.
- Updated authored raw/destination alpha fixture. No compilation, hardware, frontend build or GUI execution performed. No HDR base-color or opacity material binding, persistent material export, premultiplied alpha filtering, full PMREM, compressed formats or general custom shader ABI. Remaining QA and integration required.
