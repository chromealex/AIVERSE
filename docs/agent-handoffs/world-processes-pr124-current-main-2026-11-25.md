# World Processes integration on current main

Base `8511f30948e5834c299e433922020cf84e101ee8`. Parent PR #124 at `4abd37dda`, repair PR #184 at `5e2cbf6be`. The prior combined candidate `74cc12561` included both on the older main base `73a749b64`.

## Integration acceptance

- Combine native Studio handling of SpectraFX and World Processes, keep both inspector edit paths and preserve the current Studio window finder and dialog behavior. Preserve World Explorer, Simulation Profiler, FX Library and Caverns routing.
- Preserve the current main v11 seat-controller disk format and the pre-integration branch's alternate v11 edited spherical terrain layout. Edited terrain selects v12 on write. Restore the complete checksummed snapshot on either supported read path; unedited saves keep their existing versions.
- Verify all four flat-map EVOLVE edge handles, signed centimetre conversion, one reversible document edit on release and reopened map bounds.
- Reconcile shared registry, region tests, history, budget and simulation ownership. Regenerate the catalog from owners. No additional gameplay or editor authority.
- Current web typecheck, web tests, Vite production build and catalog generation/validation passed. Native target, complete Mac quick gate, current remote CI, interactive Scene capture and final main integration are pending. Previous PR #184 acceptance is recorded in its separate reports.

Stage commit `d90c582e3`. Mac verification on the combined sources: `npm run typecheck` exit 0; `npm run test:web` exit 0 (208 TAP tests passed, no failures); `npm run build -- --outDir /Volumes/KINGSTON/AIVERSE-builds/pr124-current-mac-8511/studio-web` exit 0. `python3 tools/skills/catalog.py check` validated 286 skills; `python3 tests/skills/catalog_test.py` exit 0. `/Library/Frameworks/Python.framework/Versions/3.12/bin/python3.12 tools/site/build.py check` reported 56 entries, zero errors. The system Python could not parse a pre-existing f-string in the site renderer. Ninja was unavailable; Unix Makefiles configured successfully. Native tests are running on KINGSTON. No release or main integration claim.
