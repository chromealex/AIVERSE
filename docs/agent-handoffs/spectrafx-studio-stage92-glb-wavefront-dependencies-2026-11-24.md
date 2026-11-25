# Stage 92 — GLB and Wavefront replay dependencies

Parent `694f057f78e13fecc56b1f80a08ce3341100f128`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

Manifest v3 includes GLB v2 first JSON chunk external buffer/image URIs and simple OBJ mtllib / MTL texture references. The GLB header and chunk bounds are checked; binary body remains covered by the existing streamed hash. Wavefront parser accepts simple unquoted local paths; ambiguous quoted paths, map options and continuation lines fail closed. Existing v1/v2 manifests retain their original algorithms. Existing limits remain: 64 dependencies, 128 MiB streamed hash budget, 8 MiB parse budget, 1 MiB per parsed document/chunk. Test source adds shared GLB/OBJ dependency and unsupported MTL option cases. No native or Node execution performed. GUID relocation, portable presets, and thumbnails remain separate work. Main untouched.
