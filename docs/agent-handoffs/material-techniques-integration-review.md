# Integration review of material-techniques

Tested checkout 35691de7c. The report docs/evidence/material-techniques-reviewed.json records focused checks on that tree. The bilingual devlog entry docs/devlog/entries/2026-11-25-render-material-techniques.json references a real native or web frame. The main integrator owns the post-merge site deployment and the merged-tree Mac quick gate.

On Apple M3 Ultra, 400 submissions arranged in a 20×20 grid at 512×512, 30 warmup and 60 measured frames. Median/P95 CPU submission and completed GPU time, renderer-tracked memory, draw count and counted C++ allocations are recorded below. Numbers capture one fixed scene under concurrent build load. Driver memory and driver allocations were not instrumented.

```text
off instances=400 warmup=30 samples=60 draws=2 cpu_median_ms=1.87512 cpu_p95_ms=1.89092 gpu_median_ms=0.09775 gpu_p95_ms=0.113916 renderer_texture_bytes=526480 renderer_buffer_bytes=2441264 cxx_new_median=5 cxx_new_p95=5

on instances=400 warmup=30 samples=60 draws=2 cpu_median_ms=2.00246 cpu_p95_ms=2.05817 gpu_median_ms=0.0917499 gpu_p95_ms=0.113375 renderer_texture_bytes=526480 renderer_buffer_bytes=2441264 cxx_new_median=5 cxx_new_p95=5
```

Off/on selects default material handling / authored techniques. Both runs use one instanced primitive. See material-techniques-web-20261125.md for the interactive checks and pixel counts. The source of the Web demo is demos/material-techniques. Multi-primitive models may lose merging when material techniques are active.
