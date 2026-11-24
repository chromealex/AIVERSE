# Stage 102 roadmap and delivery record

Base 4609df1f; World Processes integration branch. IN PROGRESS.

Scope: same native Renderer3D Scene, bounded selected-body CubeSphere preview of authored MapDoc corner heights, entity markers with terrain-occluded ray picking, orbit/zoom/focus and single entity surface drag committing body/face/x/y atomically via existing studio.setProperty and MapDoc Undo. Same-body graph overlays using actual positions. Keep planar paths unchanged and block old planar brushes/gizmos on planets. No new browser renderer or mutable document. Full model rendering/rotation/scale, live overlays, gravity field editing, region drag and GPU acceptance remain separate. Shared Studio files require integration review with studio-html5 owner. Tests deferred, no main merge.
