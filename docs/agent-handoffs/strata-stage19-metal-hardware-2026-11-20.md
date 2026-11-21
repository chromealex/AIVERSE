# STRATA Stage 19.5 — owner Mac Metal hardware validation (2026-11-20)

## Provenance and scope
- Repository: `chromealex/AIVERSE`.
- Validated local integration commit from preceding stage: `0c561cdeb42866ac1a0e4198cd11a74ae1a4300e` on `integration/strata-main-stage19`; confirm local SHA/build identity when archiving GPU logs. This handoff itself follows that commit.
- Evidence: owner-supplied macOS terminal transcript (full build and six manual executables).
- Full integration CTest previously observed: **196/196 PASS**, total 258.73 s. Hardware tests below are separate from this CTest suite.
- This stage does **not** change engine C++/GPU shaders, scene data, RHI, geometry ownership, feature flags or parity gates. It records observed evidence only.

## Actual owner Mac hardware test results

All six CMake targets built successfully; all six executables exited with code 0.

| Executable | Hardware result | Key evidence |
| --- | --- | --- |
| `aiverse_strata_metal_parity` | PASS | GPU selector: five cases, fine/coarse, frustum rejection, disabled instance and bounded overflow; `cases=5/5` |
| `aiverse_strata_metal_temporal` | PASS | Three submissions and `status=PASSED`; verifies its defined temporal fixture, not entire production warm-history rendering pipeline |
| `aiverse_strata_metal_raster_parity` | PASS | Source legacy versus lossless fine/coarse changed channels=0, real Renderer3D main/sun paths, exclusive draw, one shared GPU geometry pool |
| `aiverse_strata_metal_depth_multiview_parity` | PASS | Two independent camera views, exact main RGBA and raw sun Depth32Float for lossless fine/coarse; nonclear shadow samples 73,179 and 34,765; `status=PASSED views=2 modes=4 frames=89` |
| `aiverse_strata_metal_reflection_material_parity` | PASS | Two UV-textured opaque materials, two views, exact main/sun/reflection RTT equivalence for lossless fine/coarse; reflection RTT 120,000 pixels; nonblank; `status=PASSED views=2 modes=4 frames=89` |
| `aiverse_strata_metal_portable_parity` | PASS | Legacy versus lossless portable fine/coarse changed channels=0; shared renderer and geometry pool; `status=PASSED frames=44` |

The terminal output reported no Metal pipeline compilation failures. The submitted transcript includes log tails, not archived raw GPU capture files or a complete captured-image manifest.

## Not yet proven

- Mixed-depth **lossy** LOD geometric cracks and perceptual material/normal transitions in real scenes.
- A full temporal camera-motion visual capture across the main, sun, spot and reflection passes.
- Vulkan hardware compute/indirect/temporal/visual parity: Metal execution does not certify Vulkan.
- GLES3/WebGL2 device render fallback parity: portable recording-RHI and native Metal tests do not certify GLES.
- Real GPU timestamp/per-frame efficiency, memory budgets under load and comparable FPS benefits.
- New main commits and future merges still require regression checks; tested commit SHA matters.

## Status / next action

| Workstream | State |
| --- | --- |
| Integrated build + CTest | 196/196 PASS |
| Manual Metal hardware targets | 6/6 PASS |
| Metal lossless main/depth/reflection parity | PASS in authored fixtures |
| Vulkan hardware | NOT RUN |
| GLES hardware | NOT RUN |
| Lossy mixed-depth image audit | NOT RUN |
| Final PR/main merge | NOT DONE |

Next, run Vulkan in an actual Vulkan-capable environment when configured; test GLES3 fallback separately on a supported device/driver. Perform lossy mixed-depth seam and temporal image capture reviews with the Stage18.5 audit manifest. Keep GPU production parity toggles OFF unless the exact path and build have been independently accepted. Preserve another handoff after the next stage with tests and exact SHA. Do not misreport this evidence as full cross-backend acceptance.
