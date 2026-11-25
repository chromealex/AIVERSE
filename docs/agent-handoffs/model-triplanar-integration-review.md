# Integration review of model-triplanar

Tested checkout fe35dd3cc. The report docs/evidence/model-triplanar-reviewed.json records focused checks on that tree. The bilingual devlog entry docs/devlog/entries/2026-11-25-render-model-triplanar.json references a real native or web frame. The main integrator owns the post-merge site deployment and the merged-tree Mac quick gate.

On Apple M3 Ultra, 400 submissions arranged in a 20×20 grid at 512×512, 30 warmup and 60 measured frames. Median/P95 CPU submission and completed GPU time, renderer-tracked memory, draw count and counted C++ allocations are recorded below. Numbers capture one fixed scene under concurrent build load. Driver memory and driver allocations were not instrumented.

```text
off instances=400 warmup=30 samples=60 draws=2 cpu_median_ms=0.305084 cpu_p95_ms=0.314375 gpu_median_ms=0.150875 gpu_p95_ms=0.197333 renderer_texture_bytes=526480 renderer_buffer_bytes=2441264 cxx_new_median=8 cxx_new_p95=8

on instances=400 warmup=30 samples=60 draws=2 cpu_median_ms=0.319417 cpu_p95_ms=0.326083 gpu_median_ms=0.163375 gpu_p95_ms=0.202959 renderer_texture_bytes=526480 renderer_buffer_bytes=2441264 cxx_new_median=8 cxx_new_p95=8
```

Off/on selects colour-only / colour plus tangent-space normal projection. Both runs measure the same geometry and repeat scale. See the bundled web game and the browser evidence for the camera controls.

Authored gallery and camera code 377ac53c0. Site and native acceptance SHA e81bf47b9. The standalone AIVERSE packager built 56 entries including the UV-free GLB and both authored stone textures. Pack SHA-256 3be0465271c90b7ab6e123b8acdb1cb94b05ea8b6a93434aaa13d82828f31d0a. In a browser compatibility run the packed project was mounted by the existing shared WebGL2 player from the material-techniques build. Only its preload extent was updated to match the new pack. No shader or engine code was added to the player. Chromium rendered the stone pattern over the exterior and inside the opening. Real RmlUi Front / Side / Zoom out clicks gave 84,691 / 74,569 / 34,691 brown-toned pixels in crop x=360..919, y=65..499 (r>g+2 and g>b+2). Compared against Front, 141,120 pixels changed in Side and 172,603 in Zoom out at >10 RGB levels. The three actual screenshots are stored in docs/evidence/model-triplanar-web-{front,side,wide}.png. Other graphics backends and physical mobile hardware were not exercised.
