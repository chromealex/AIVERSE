# Stage 79 — checkpoint transfer lease consistency

Branch `feature/spectrafx-html5-data-interfaces-20261123`, parent `eb1af634291e739b70cca6c9a5d5e43e20110ea2`.

Browser export captures a positive safe integer transfer ID before validating metadata, so finally cancels a known lease even if size/error validation fails. File size must be a positive integer within the existing SPFT cap, and each read response must agree with the initial size. Native begin rejects an active export, matching the existing export rejection during import. No checkpoint format or GPU ownership changes.

Source inspection only; native build, Node/Vite, live Studio/MCP transfer tests not run. Stage 78 CI was still queued. Next verify malformed responses release leases, mutually exclusive transfers, cancellation, timeout and full hardware acceptance. Main untouched.
