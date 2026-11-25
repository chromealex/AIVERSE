# World Processes integration on current main

Base `8511f30948e5834c299e433922020cf84e101ee8`. Parent PR #124 at `4abd37dda`, repair PR #184 at `5e2cbf6be`. The prior combined candidate `74cc12561` included both on the older main base `73a749b64`.

## Integration acceptance

- Combine native Studio handling of SpectraFX and World Processes, keep both inspector edit paths and preserve the current Studio window finder and dialog behavior. Preserve World Explorer, Simulation Profiler, FX Library and Caverns routing.
- Preserve the current main v11 seat-controller disk format and the pre-integration branch's alternate v11 edited spherical terrain layout. Edited terrain selects v12 on write. Restore the complete checksummed snapshot on either supported read path; unedited saves keep their existing versions.
- Verify all four flat-map EVOLVE edge handles, signed centimetre conversion, one reversible document edit on release and reopened map bounds.
- Reconcile shared registry, region tests, history, budget and simulation ownership. Regenerate the catalog from owners. No additional gameplay or editor authority.
- Current web typecheck, web tests, Vite production build and catalog generation/validation passed. Native target, complete Mac quick gate, current remote CI, interactive Scene capture and final main integration are pending. Previous PR #184 acceptance is recorded in its separate reports.
