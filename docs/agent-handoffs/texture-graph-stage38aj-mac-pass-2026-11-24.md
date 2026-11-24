# Stage 38AJ — Mac acceptance summary
Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Expected checkout: `f1a2894cdf0488857a41d4e38afd9ca4dc1ed8a8`

Stage 38AJ Mac targeted acceptance: user supplied final CTest lines `32/32 Test #493: studio_model_picking ... Passed 0.01 sec` and `100% tests passed, 0 tests failed out of 32`. Record user-supplied summary PASS 32/32 (includes material_batches_unit in the requested run); full log, printed checkout SHA, tracked tree status and individual timing evidence were not supplied. Expected checkout was f1a2894cdf0488857a41d4e38afd9ca4dc1ed8a8; not independently established. Do not promote the feature gate: live Studio/MCP, Vulkan hardware and merged-tree Mac quick gate still NOT RUN.

Next Stage 38AK: build aiversemcp in the same configured external build and execute tools/mcp/audit_registry.py against the graph-framework manifest. This is actual headless MCP discovery/call evidence, not live GPU proof. Then verify matching Studio host with a disposable project, native GPU preview/read/cancel/material apply and no silent CPU fallback. Do not rerun the 32-test suite solely to collect the missing log if the original log remains available. No main merge.
