# PR #183 graph GPU project lifetime gate — 2026-11-25

Stage commit: `64061c8a2ac84a9feb24c6c940888b04054ea8d4`. Changed path: `apps/editor/core/TextureGraphGpuPreview.h`. The integration code at `c6cfd6cf2` introduced `Editor::syncGraphOwnerProjectLifetime()` but the current-main GPU owner adapter lacked the old branch's `invalidateProjectLifetime()` method. This stage delegates invalidation to the existing `TextureGraphGpuOwner::checkProject("")`, which clears queued work, receipts and an active project-scoped request on project reopen. No new GPU solver or RHI allocation path.

Actual gate: `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr183-publication-20261125/mac --jobs 8 --ctest-jobs 4` configured and built until `apps/editor/EditorPlayOps.cpp:18` failed to compile against the prior stage. Report `/Volumes/KINGSTON/AIVERSE-builds/pr183-publication-20261125/mac/test-reports/20261125-172357-5161`. CTest was NOT RUN by that gate. The editor target rebuild on this SHA is in progress; no success claimed yet. Earlier native `vfx_graph_ops` and `studio_session` passed on `630793aca`.

Next: finish editor target rebuild, rerun the full merged-tree Mac quick gate on a clean committed checkout, then update manifest evidence. Windows, performance and actual Studio GUI acceptance remain open. No main merge yet.
