# Universal cameras — Stage 1 handoff (2026-11-19)

## Ownership and branch
- Feature branch: `feature/universal-multicamera-render-targets`; forked from `main` at `58e29ab052458fffcdebebffe27022b53adb9367`.
- No changes to `main`; no force-push.
- Implementation commits (nine parts of ONE mechanical migration stage):
  `7d1222c2ec393bd60cd046681df6a9802dd45701`,
  `bf55bf87d9dd5cf80f1b458529e896ccdf7ff09c`,
  `fc8d390160298f3e7b74e5c796dbf8bcd9ac06da`,
  `f8c5a779fc4fea10d5059d298c0a22b98db63713`,
  `d0b9d387930a6456efa752a5bc3f9d65c052e43f`,
  `5fd8d3a24994ad6f4e3892dd6190d683561a6675`,
  `3e0fda073b4abcc9820442ded602a23aaa0332f5`,
  `13c693ba207d6769708197d49b3bf25659b78554`,
  `94834fa57a303b747d74c338e26a1b50ff024868`.

## What changed
- Renamed the generic orbit/perspective/orthographic camera type from `aiverse::render::RTSCamera` to **`aiverse::render::Camera`** in `engine/render/Math3D.h`.
- Updated 67 source/headers/tests using the old type (total migration set 68 files), including render, lighting, FX, editor, native game hosts, FPS, voxel, RTS adapters and examples.
- The generic gameplay camera mode `aiverse::lib::gameplay::camera::Mode::Rts` (unused by call sites found during source search) is now `Mode::Free`, avoiding a genre name in a shared helper.
- No RTS alias remains in the general renderer API. Genre-specific wrappers/files under `modules/rts` remain genre-specific, which is intentional.
- Historical `*.bak` files and old handoffs were not rewritten.
- Existing `Camera` data layout and math were otherwise not changed; existing render targets `gfx::PassDesc` are unchanged.

## Affected surfaces
`engine/render/{Math3D,Renderer3D,Lights,ScreenLighting}*`, `engine/fx/*`,
`apps/{client,editor,modelview,place,renderbench}/*`,
`modules/{fps,rts,voxel}/*`,
`examples/*`, `tests/*`, `libraries/include/aiverselib/gameplay/Camera.h`.

## API / compatibility
- All **source** users must now spell `render::Camera` (or unqualified `Camera` in the render namespace) rather than `RTSCamera`.
- The class remains a value type with `target`, `distance`, `pitch`, `yaw`, `fovY`, `ortho`, `zNear`, `zFar`, `jitter`, `eye()`, `view()`, `proj()` and projection helpers.
- Breaking source rename; no binary ABI guarantee or serialized format migration is claimed.
- Do not add back `RTSCamera` into `engine/render`. Any migration alias must be isolated to a genre-specific adapter and temporary.

## Tests actually performed
- Remote GitHub source review and tree/commit creation only.
- **NOT RUN**: CMake configure/build, CTest, macOS Metal, Vulkan, GL, editor smoke tests or render pixel comparisons. Remote connector cannot execute the owner's Mac checkout.
- The change must **not** be declared compile-validated without a real build.

## Risks / next step
- Default-branch GitHub code search was used to find the migration set. It can miss references outside indexed files, generated code and scripts; perform a full `git grep -n 'RTSCamera' -- ':!*.bak'` on a freshly fetched branch checkout.
- Compile all targets to catch hidden references, ambiguous `Camera` names, and API consumers; run `ctest --test-dir build/<chosen> --output-on-failure` and Metal/GL RT smoke tests.
- Continue **Stage 2**: extract the genre-free portrait render target + model pose/material preview from `modules/rts/view/RtsPortrait` to `engine/render/PortraitRenderer`, leaving only `RtsView` and `UnitType`-based adaptation under `modules/rts`. Make a **separate** Stage 2 implementation commit, then a **separate Stage 2 handoff commit**, per `AGENTS.md`.
- Continue **Stage 3**: generic multi-camera render-target scheduler with per-camera state and no genre references; isolate per-camera post FX history and recursion hazards; document, then test together per user's preference.
