# AIVERSE mandatory render-contract rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## Rendering ownership contract — PRISMA vs SpectraFX (MANDATORY)

**Never implement the same screen effect in both systems.** PRISMA and SpectraFX are distinct genre-independent engines:

| Owner | Owns | Must NOT own |
|---|---|---|
| **PRISMA** (`aiverse::render::Prisma`) | SceneColor/depth/velocity/history-dependent **full-screen post-processing**: HDR resolve, exposure/tonemapping, bloom, DoF, motion blur, TAA/upscaling, color grading/LUT, lens effects, screen-space GI/AO/SSR where implemented, atmospheric screen passes, quality profiles, post-process camera volumes, render passes, graph scheduling, GPU cost diagnostics, and the `postfx` Shader Graph domain | Particle spawn/update/lifetime, emitter graphs, VFX triggers, mesh/ribbon particle simulation |
| **SpectraFX** (`aiverse::spectra`) | **World/scene VFX emitters and simulation**: particles, meshes, ribbons, smoke, fire, explosions, trails, ECS event presentation, CPU/GPU emitter scheduling and graph assets `fx/graphs/*.vfx.json` | Post-processing of SceneColor, camera volumes, tone mapping, bloom pipelines, screen-space temporal effects, `postfx` Shader Graph effects |

**Composition contract:** SpectraFX emits scene-referred linear HDR contributions into the renderer **before** PRISMA composite/tonemap, with depth/alpha semantics preserved. PRISMA processes the completed scene **before** UI/HUD. PRISMA owns any postprocess that enhances particle appearance (e.g. bloom), while SpectraFX owns the emissive particle input. Neither system depends on FPS/RTS/other genres or modifies authoritative ECS data. Shared RHI and generic Shader Graph compiler stay shared; the `particle` domain belongs to SpectraFX, the `postfx` domain to PRISMA.

**Official PRISMA agent skill:** [skills/visuals/aiverse-prisma/SKILL.md](skills/visuals/aiverse-prisma/SKILL.md). Every post-processing or screen-space image request must use it, not the SpectraFX skill.

**Canonical paths:** PRISMA `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`; editor `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp`; roadmap `docs/prisma.md`. SpectraFX `engine/fx/SpectraFX.h`, `engine/fx/Spectra*.{h,cpp}`, `libraries/include/aiverselib/mechanics/SpectraFX.h`, roadmap `docs/spectrafx.md`.

**Compatibility is intentional:** `PostFx*.h`/editor `PostFxOps.h` are forwarding legacy includes and `PostFx*.cpp` are empty shims. Keep the external `"postfx"` JSON key, `postfx_*` MCP operation names, `view_postfx` and editor saved ID `"postfx"` until a separately approved versioned data migration. Do not create a second implementation or double-register the old editor window. Legacy `PostFx` types remain source aliases to PRISMA. Old source filenames must never be linked instead of the new canonical PRISMA .cpp files. The optional `aiverse_postfx_data` CMake target remains as compatibility link name.

**Agent handoff:** Every PRISMA implementation stage MUST have a **separate, committed** `docs/handoffs/prisma-stage-XX.md` on its working branch, naming code changes, public APIs, real test commands/results, untested paths, current SHA and precise next step. Never mark unrun tests PASS. SpectraFX has a separate living handoff `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`; do not overwrite it with PRISMA changes. Any shared-render integration must be documented in both systems' design docs and note the exact common interface.

## PRISMA post-processing rules (short reference)

- Canonical ownership and compatibility contract: **see the mandatory section at the top of this file**.
- PRISMA keeps GPU-free effect policy/data, volume blending, graph scheduling and optional ECS presentation adapters independent from FPS/RTS; all renderer work uses shared RHI.
- After each stage: `docs/handoffs/prisma-stage-XX.md`, with full changed paths, architecture, real tests/status and next step.
- Roadmap `docs/prisma.md`; legacy format details `docs/postfx.md`.
