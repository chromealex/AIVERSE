# Material techniques on Metal

Code commit e2f8f43e2d112ec8b414a99606b055cbb9624102. Native source and demo paths appear in the feature manifest. This stage links the GPU test to the client runtime archive and keeps the single-player gallery open for viewing.

The focused CTest run exercised model_batching_policy and material_technique_frame. Both passed. The Metal device reported 14 pipelines and zero failures. Each 192×192 readback contained 7,716 pixels dominated by the selected channel. The amber near, blue far and green inspection PNGs are committed under docs/evidence/material-techniques-*.png. The JSON provenance and full run output live under docs/evidence/material-techniques-runtime-20261125.*.

Next, verify native MCP and browser authoring, run the packaged Web demo, and record the remaining feature checks before the integrator's merged-tree Mac gate.
