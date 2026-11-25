# Stage 81 — deterministic checkpoint lease lifetime

Parent `f460c6370311335b9afb02f07e690d109cda1ff3`, branch `feature/spectrafx-html5-data-interfaces-20261123`.

Extracted render-owner transfer lease state with explicit monotonic time, 30-second inclusive expiry, queued-import protection, and capacity release on reset. Cancel, expiry, stop, graph load and deferred import consumption use the same reset. EOF read no longer refreshes the lease. New GPU-independent CTest `studio_spectra_transfer` is registered in native Studio and the focused workflow. Test authored; no build or hardware execution claimed in this commit. Next run focused acceptance and check pending import when viewport is detached. Main unchanged.
