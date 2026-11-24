# Stage 97 — verified leaf asset reuse

Parent `19b7661ffe112aec1fb622a93a6b49660db6ab00`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

Offline package preview/apply accepts a reuse map of original content paths to target GUIDs. Leaf-only packages can automatically reuse uniquely relocated same-GUID resources; explicit Asset Picker reuse permits a different GUID only with identical bytes, extension and importer metadata except identity. The recipe and dependency manifest are remapped, no target asset is modified. Nested-model/JSON packages are conservatively excluded from relocation. Plan revision binds reuse selections; apply recomputes the plan. HTML5 picker uses native asset catalog, selection invalidates the previous plan. Python regression source covers automatic relocation, explicit equivalent GUID and changed bytes. No tests/build run. External package Drag & Drop and GPU thumbnails remain future work. Main untouched.
