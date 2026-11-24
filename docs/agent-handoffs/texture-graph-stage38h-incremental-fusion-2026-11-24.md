# Stage 38H — incremental pointwise fusion

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `200697f6956c55647f367aa8ec0ca3ae7dda3efd`
Implementation: `224699615439a6c845966befdf44de7f36beca8d`
Final implementation: `b63fe082ab36044b30682eac216240e4ed9a728c`

Stage 38H: incremental:true and fusePointwise:true can now be combined in native materialized execution, MCP start/enqueue/plan and Studio. Dedicated slots retain reduced-DAG region roots only, within the existing 32 MiB DAG and resident 64 MiB total caps. Exact selected-backend kernel source includes absorbed node parameters; ordered external slot dependencies propagate dirtiness through the reduced DAG. Prior complete matching layout/device/scope and source-key budget still required. Unchanged regions skip dispatch; outputs still pack/copy/validate normally. incrementalReusedPasses and incrementalDispatchedPasses count region passes, legacy incrementalReusedNodes remains a pass-count compatibility alias. No per-node timing or output texture reuse yet. Fixed temporary string lifetime in incrementalKeys. Exact-source isolated fusion and lowering tests passed GCC and Clang; real-RHI combined regression authored but NOT RUN. No performance measurements.

## Actual evidence

Exact Git blobs reconstructed locally and verified using git hash-object:
- TextureGraphDagLowering.h: 75e9891eeffbd33aea5f321835581a40fd71e2cc
- TextureGraphFusion.h: 5ca5b6fd0990810966c73aed1d45e0c8ccc6d302
- TextureGraphIncremental.h: 9264a7d5efd3dd6487d74200b5054d95d8c55dcf
- TextureGraphLiveness.h: 313d7640cb2c1fccc65745d5569ccf1d80dc1995
- TextureGraphPassSchedule.h: c298267e63b69c59f805f9e7ecfa49dc45e33097
- texture_graph_fusion.cpp: 986bd8e7f9f56e67505ac0f80aef7e9b0f23fd93
- texture_graph_dag_lowering.cpp: 92da39da5b9b6d2d9042961d92c3d907b287b98c (unchanged from Stage 38C)

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_fusion.cpp -o fusion && ./fusion
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_dag_lowering.cpp -o dag && ./dag
done
```

GCC 14.2.0 and Clang 17.0.0: both fixtures PASS on both compilers (four successful executions). Fusion fixture covers retained region slots, cold/unchanged keys, changed absorbed-node key invalidating downstream regions, leaf-only dirtiness and temporary string callback. These are CPU policy tests, not integrated shader generation/dispatch tests. Real-RHI combined fixture asserts cold 3/0 dispatched/reused, unchanged 0/3, changed 2/1; not run. Native build, Studio, MCP, actual GPU and Mac gate NOT RUN.

## Next

Add incremental final texture generation with explicit lifetime/ownership design: never reuse a published mutable image while old snapshots or GPU packets can read it. Continue automatic reclamation root audit. Actual native acceptance required before merge.
