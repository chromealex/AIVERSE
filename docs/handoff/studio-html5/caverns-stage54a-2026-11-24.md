# Caverns Stage54a — HTML5 document authoring

Date: 2026-11-24. Studio branch feature/studio-html5. No main merge.

Backend dependency: feature/terraweave-caverns-core-20261123 commit cb7ecc7f787362505c8a87fb64d90aedc65c8211 (shared Workspace operation adds exact decimal IDs/revision and optimistic edit guard). Studio alone does not contain that backend, and discovery deliberately reports it missing until branches are integrated.

Studio commits: 3c6e4fe93c038458884df538167334220770212d (allowlist/discovery), 16543c42e3b475acfb7840103b21fcd260df2248 (project/map/revision binding, get read-only classification), 530f09239d856dc26c7505306ea7ceeab644df97 (typed expectedMap), 5cfce537961d42858be5e1f2c6f8a07504b4a905 (CavernsPanel using shared UI), 352fb4abec639ba873ee48b3a4e1ca4288808ed9 (dock registration), 05c99f0f84ae65ae5ac84d154b133fe45f9a8abd (render registry), 6e0963eb9ae9020beada05fbf5b99fa9d82cd537 (skill).

The user opens Panels > Caverns, loads the actual map document, enters signed cm XYZ and brush radius, selects carve/fill, applies one stamp, or erases a selected exact ID. Existing global Undo/Redo remains authoritative. Generator and entrance information is read-only. After history or map changes refresh explicitly. Browser does not generate SDF or claim GPU readiness. Project/map guard rejects switched paths; same path/unsaved document identity still needs stronger native session fencing. The existing legacy brush is not transplanted to Studio in this stage.

Next: integrate backend branches, native Scene picking/raycast and brush stroke lifecycle, actual visual preview GeometryScene owner, generator/entrance editing and paging diagnostics. Keep approved UI kit; do not duplicate engine or renderer. No native compile, npm typecheck/build, web tests or screenshots executed. Not feature complete.
