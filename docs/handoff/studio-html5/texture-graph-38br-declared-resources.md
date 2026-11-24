# Stage 38BR — named resource declarations and typed data tables

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `de31faeac3c47f360f5aa866143aa0f7a05ac55b`. No main merge.

- Named typed port aliases and up to 16 float/int/bool parameter declarations for custom RPN. Strict slot/type/name validation, fixed token lowering, no user shader syntax interpolation. Existing three input slots and specialization/cache keys preserved.
- Independent scalar/color table nodes (1..4096 normalized entries) use immutable float4 source snapshots and existing CPU/GPU materialized upload/accounting, skip HDR mapping, expand nearest into graph field. Studio JSON inspector for declarations and table data. Authored CPU expected-value/type-rejection and strict GPU parity fixture.
- No compilation, execution, hardware or GUI verification performed. This remains bounded declarative graph compute; no arbitrary MSL/GLSL, extra raw native resources, raw buffer index access, shader reflection or writable resources. Remaining full HDR output, compressed formats, global dependencies, full cubemap PMREM and consolidated QA.
