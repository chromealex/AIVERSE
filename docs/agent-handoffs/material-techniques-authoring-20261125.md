# Material technique authoring verification

Tested runtime code e2f8f43e2. The prior Metal handoff is 1d9f7d035.

A cleanly linked aiversemcp on macOS reported 426 live tools. The audit agreed with mcp_registry and found material_techniques_get and material_techniques_set. A temporary copy of demos/material-techniques exercised first save with no existing sidecar, validation of an unknown material, rejection of stale expected rules, byte preservation on both failed saves, clearing the rules, reopening and preserving unrelated JSON keys.

Chromium mounted the product MaterialTechniquePanel. The test harness routed its existing studio.op messages to the real standalone server on a temporary project. Load returned two rules. Save changed the distance from 20 to 24, Undo restored 20, Redo restored 24, and reloading from disk returned 24. The captured inspector is docs/evidence/material-techniques-inspector.png. This test did not launch the native Studio shell. Studio TypeScript and the production bundle also passed.

Web packaging and browser play remain under verification. The final merged-tree quick gate is owned by integration.
