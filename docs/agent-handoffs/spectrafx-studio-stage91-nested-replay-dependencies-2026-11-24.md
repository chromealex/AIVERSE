# Stage 91 — versioned nested Replay dependency manifests

Parent `f7fcd67bbcfffea2089662153bae0b2929ad08b4`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

New named presets use dependencyVersion 2. Recursively follows explicit content/ strings in JSON assets and external local glTF buffer/image URIs. Embedded data is covered by the parent hash. Deduplication/cycle protection, 64 paths, 128 MiB hash budget, 8 MiB JSON parse budget, per-JSON 1 MiB cap; unsupported URI forms and paths escaping content fail closed. Existing v1 direct manifests still compare with their original algorithm; no silent rebaseline. UI/parser recognize manifest version; MCP check reports scope. Regression sources cover a cyclic JSON/glTF graph, external buffer same-size edits and escaping URI. No tests/build executed. GLB/OBJ/MTL and arbitrary relative material references, GUID remap, portable presets and thumbnails remain future work. Main untouched.
