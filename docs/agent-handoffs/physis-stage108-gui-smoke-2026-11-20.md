# PHYSIS Stage108 — unlocked Mac GUI smoke

Date: 2026-11-20. Branch: codex/physis-qa-fixes.
Tested product SHA: 076139d12d9ea2380a5445f823323f4e2c096811.
Report commit: e13988b8e72fe2a5497d897ea257dd5ba8c6b54e.
Previous: [Stage107](physis-stage107-qa-fixes-2026-11-20.md).
Report: [GUI evidence and limits](../qa/physis-gui-resume-2026-11-20.md).

Documentation-only stage; no product, public API, save format or renderer change. Rebuilt native editor successfully on KINGSTON and operated uniquely named QA app through CUA. Actual button sequence observed: create25 → step26 → +60=86 → reset25. Humidity selection changed raster; clicked cell X-3/Z-6 displayed0.4093. Six scoped GUI cases PASS. Screenshot evidence is in the task conversation, not an exported artifact. Prior30+2 native PASS remains separately scoped; tests not rerun here. Remote CI NOT CHECKED; no CI URL claimed.

Initial timed app launch exited and app targeting timed out; recovered by reopening uniquely named bundle and disposable project. Pixel scrolling unsupported; used scrollbar clicks. No product defect inferred from failed automation drag. QA executable stopped by exact path. Concurrent user editor preserved. Shared local prefs/registry had concurrent changes; backup retained locally instead of blanket restore. See report for exact cleanup paths and remaining QA recent-project entry.

Next action: publish report/handoff preserving concurrent main work; separately validate real host integration and volumetric visuals against user references. This smoke does not close those acceptance gaps.
