# Stage 38BN — transactional dependency refresh

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `e2873fa72c25ab06f5d7e970098215dc887d1d93`. No main merge.

- Native bounded hash-only dependency revisions action reuses project path/symlink and encoded read validation. Max 64 paths and 64 MiB reads per scan, per-file 16 MiB. Reports unavailable sources, no writes or decode.
- Studio bulk refresh and opt-in idle five-second polling; roots and nested embedded literal image pins refreshed atomically after native validation, edit-history and semantic preview invalidation. No mutation of saved documents, queued GPU snapshots, published PNG sets or live material bindings. Parameter-bound asset/revision paths explicitly excluded.
- Linked asset refresh uses existing interface and closure remapping, exact session baseline to prevent overwriting local edits. Baseline is established by explicit link/refresh or matching remote closure; incompatible or unverified changes require explicit intervention. No recursive disk propagation into unopened graphs or automatically replacing published outputs.
- No build, frontend test, native execution or GUI/MCP verification performed. Remaining: headless/project-wide dependency scheduler, GGX/BRDF, full HDR export, ASTC/BC6H/Basis and general custom resource ABI. Final consolidated QA pending.
