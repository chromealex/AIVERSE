# Caverns Stage54a — HTML5 Studio document authoring

Date: 2026-11-24. Main unchanged. Caverns branch remains separate from feature/studio-html5.

Caverns commits: cb7ecc7f787362505c8a87fb64d90aedc65c8211 (shared native Workspace operation exact string identifiers and optional expectedRevision guard), 0418366ae76e9f25123398cb3a80e6712bc15c4a (skill). Studio branch handoff 650cd121c0dbfd78a393d1ba0a9d8a138adb54f8; details in docs/handoff/studio-html5/caverns-stage54a-2026-11-24.md on that branch. Studio allowlist, project/map guard, typed request and dockable panel implemented. The native backend is not in standalone Studio yet; discovery reports unavailable until integration. No merge performed.

Next Stage54b: integrate branches without losing concurrent Studio changes; native 3D brush picks using existing SDF raycast, stroke grouping, GeometryScene preview and diagnostics; generator and entrance mutation, paging policy settings. Document-only panel currently supports coordinate Carve/Fill/erase and read-only generator/entrance data. Actual preview and native gameplay ownership are not claimed. No native compile, npm typecheck/build, tests or screenshots run. Complete remaining feature work before mandatory KINGSTON QA.
