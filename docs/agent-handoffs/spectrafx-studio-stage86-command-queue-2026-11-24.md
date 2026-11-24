# Stage 86 — bounded browser command queue and native dispatch telemetry

Parent `c7dbd702b637ea37b79ed2fbaae50d7ec5f9ab2b`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

All HTML5 preview RPCs use a per-mounted-panel queue (32 waiting max, one in-flight). Identical idle status requests coalesce without crossing a queued mutation barrier; mutations and cancellation take precedence over idle status while preserving their FIFO order. Stop supersedes undispatched work; unmount rejects queued work. Already submitted native operations cannot be interrupted. Load waits for the initial native status identity. MCP still runs on the existing Studio UI-thread agent poll and shares the native dispatch function with HTML5; no second render thread or host-wide asynchronous priority queue was introduced. Native responses include actual dispatch sequence, action, elapsed milliseconds and cumulative failure count, shown in the panel.

Node regression tests registered in test:graphs (serialization, coalescing, Stop, close, failure recovery). No Node/Vite or native tests executed in this stage. Next validate full graph suite and audit multi-step replay cancellation across concurrent MCP operations. Main untouched.
