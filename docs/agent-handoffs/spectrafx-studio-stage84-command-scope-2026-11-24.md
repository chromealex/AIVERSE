# Stage 84 — project and runtime generation command guards

Parent `d4769ae40ac8fbc65bcb24450da24392092cfdc2`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

StudioHost rejects stale optional expectedProjectPath and expectedGeneration before dispatch (including capture/camera). Existing MCP callers remain compatible; fx_graph_preview advertises the new optional guards. HTML5 preview remounts per project and wraps every preview RPC with the captured project path and mounted guard. Manual commands, checkpoint start/restore, capture and replay sequences use expected generation when available. Auto-reload resumes only the generation it just loaded. Idle status polling stops during busy commands. These changes do not retract an already-dispatched native operation, and per-scene generation currently resets when a device is recreated; stable host-wide instance identity and same-path reopen need further review. Source-only, no Node/native tests executed. Main untouched.
