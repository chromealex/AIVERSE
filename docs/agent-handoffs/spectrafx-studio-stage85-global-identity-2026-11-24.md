# Stage 85 — process-wide preview and transfer identity

Parent `fec5aab39b743d3263fe6e8f1a9d31d7f9a0e2ba`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

Runtime generation and transfer IDs now share one monotonic atomic process-wide allocator, capped at JavaScript safe integer range. Identity never restarts on scene recreation, Stop, graph replacement, or another transfer. Successful project open destroys the previous FX scene on the render owner even for the same path. HTML5 polls idle status before readiness, supplies known generations to load/replay and all checkpoint transfer operations, and preserves compatibility when no identity is known. CPU contract checks uniqueness; native fixture no longer assumes consecutive generations and checks independent scene identity.

No full native or Node build run in this stage. A first command before initial status has no generation guard, and the existing host dispatch is not a new prioritized asynchronous queue; these remain follow-up items. MCP optional guards from Stage 84 remain compatible. Main untouched.
