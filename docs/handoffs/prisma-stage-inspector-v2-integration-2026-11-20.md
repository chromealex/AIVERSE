# PRISMA / Inspector VFX composition checkpoint — 2026-11-20

Branch feature/aiv20261120_perf_inspector_integration; implementation `9db6d2014137e41cd67ff82dc187cf1136f73bb3`.
Only Editor composition changed: optional drawSpectraGpuPreview before resolvePrisma,
keeping main reactive coverage, volumes and GPU timing. PRISMA source/public JSON
and legacy postfx names unchanged. Full details: `docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`.
Native editor build and SpectraFX 20/20 PASS; real Metal/PRISMA scene and actual
frame review 5/5 PASS. Full repo/Release/Vulkan/GLES/manual UI NOT RUN.
Next: publish reviewed merge, then interactive preview QA and platform CI.
