# Stage 58 — preview opt-in and checkpoint RNG metadata

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 5439d34b8f98ccb8979b83560a6966c6dabd197f, 98a6f232f378c97acb08997367d3a34a7e65e19e, 5f6abe11f3183f3c96185cbe1f33cea3dd48bea8.

SpectraNativePreview.tsx adds explicit GPU birth random checkbox and passes option on load/reload. SpectraGpuCheckpoint.h and SpectraGpuParticles.cpp capture RNG metadata in memory at asynchronous checkpoint request time. Skill updated. No tests/builds. V2 diagnostic binary export unchanged; RNG restoration and preset opt-in persistence not yet implemented. Next audit checkpoint restore/export and exact capture provenance; finish before deferred QA.
