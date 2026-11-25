# Stage 38BM — nested texture functions

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `3329de83660fe2ab0429f82f10ce7a477d238a26`. No main merge.

- Shared library nested function calls, output type resolution, local port and cycle validation, whole-library recursion/depth guard (8 levels), deterministic recursive flattening within existing 64-node/work budgets. Retains outer result IDs. Explicit argument forwarding via arguments.<name> with child compiler validation; forwarded bindings cannot mix with primitive property bindings.
- Studio resolves nested call types, captures selected calls, recursively counts expansion, guards deletion of referenced definitions, publishes closure and remaps linked private helpers. Refresh checks external helper callers; unused obsolete helper snapshots may remain embedded. Dependency diagnostics now include nested calls and recursion.
- Authored nested instance CPU expected values and strict GPU fixture; no build, CPU/GPU execution, frontend/GUI/MCP verification performed. Native parameter defaults in unused definitions are still validated on instantiation. Direct formal-input result aliases unsupported as before. Dependent asset refresh automation, full HDR, ASTC/BC6H/Basis and custom resource ABI, GGX/BRDF remain open.
