# Local Studio play operations

Implementation commit `82dbb8923af8679e4f7bf77df4aff0919434ac2c`.

The editor workspace declares seven typed operations in `apps/editor/core/StudioPlayTools.cpp`. Only the local Studio host binds them to a running session. Runtime Debugger and Play Mode Review reuse the native UI handlers and their project, session and version checks. Agent and UI review previews have independent tokens. The schema rejects unknown properties before dispatch.

The Studio Python contract suite completed 55 cases with one bundled-web case skipped in the run after this change. The strict skill catalog audit passed. All ten delivery manifests validated structurally. Native Mac and Linux builds started on this commit. Live paused-game edits, preview application and the merged-tree Mac quick gate still require actual results.
