# Stage 80 — checkpoint transport error isolation

Branch `feature/spectrafx-html5-data-interfaces-20261123`, parent `bd87ab7044ee377b29c2d14c9677b3b67e5a6df6`.

File begin/read/append/export/import/cancel responses use request-local errors rather than mutating the simulation error latch. Successful file transport responses no longer clear existing runtime errors. Status polling expires idle leases; malformed reads/appends cannot extend lease lifetime. Pending checkpoint command rejection is also request-local. Actual deferred GPU restore failures still use the existing runtime error path.

Metal Studio regression source checks that a rejected import while playing does not stop native ticks and that stale cancellation does not overwrite an existing error. Source review only, tests not executed. Next verify with native Studio acceptance and add deterministic lease clock tests. Main untouched.
