# Stage55 — hydration and native RPC audit
Branch: feature/world-processes-stage31-bidirectional-handles. Main unchanged.
Hydration previously truncated Fixed map positions to cm while Studio rounded. Changed hydration to nearest cm, ties away from zero (93e66d4e). Native numeric preview now rejects hidden/locked selections and validates integer bounded custom pivot (03e5dcd0).
Reviewed hydration ordering: EvolveRegion before EvolveArea, then map position binding and ECS area validation. TerraWeave consumption of ECS extents is described in hydration source; actual consumer implementation and runtime parity not verified.
No builds or tests. Next: audit TerraWeave consumer and hydration preflight/atomicity; check Fixed versus editor float quantization at boundaries. Do not merge before QA.
