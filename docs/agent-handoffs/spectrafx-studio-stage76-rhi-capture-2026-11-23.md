# Stage 76 — RHI capture error handling

Branch feature/spectrafx-html5-data-interfaces-20261123. Commits d99a7d437b113b3c5a48400a8eee20bab04c9a44, 282c21445e940c7f2bf7825377753dba54414db0, 457ef45bc81660a80de04d1e9fbd935f711b9bd3.

Vulkan GfxVulkan.cpp guards readback allocation and avoids present/slot advance on failed submit. Metal GfxMetal.mm guards allocation/blit and aligns rows. GL GfxGL.cpp rejects zero dimensions and restores pack/framebuffer state (shared GLES backend). No builds/tests. Verify device-loss teardown, outstanding async readbacks, GL state and Metal odd-width capture in acceptance. Next static API audit then main integration. Not merged.
