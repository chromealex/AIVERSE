# Shadow filter noise — 2026-11-20

## Stage 1: diagnose and gate spatial random rotation (incomplete visual fix)

Branch: `feature/studio-html5`. Base: `2f9c1b01ca07140dfecba2cf7d994881f1b5b5cf`.
Code: `7dd7edb16f48e355a236a47c5bd4c9144a56d8c3`.
Paths: `engine/render/Shaders.h`, `tests/render/temporal-pcss.md`.
Both GLSL/MSL now require the existing fractional quality.x temporal capability for spatial rotation. Low, sample budgets, receiver-plane bias, PCSS blocker search, public API/save format remain unchanged.

Validation performed on the code tree above before commit:
- `python3 tests/render/check_model_lod_shaders.py`: PASS 32 GLSL/GLES links.
- CMake Debug build in `/private/tmp/aiverse-shadow-noise-20261120`: PASS targets aiverse_hdr_scene, aiverse_hdr_gl, aiverse_hdr_policy. Configured with HDR tests ON and editor/tools/client/server/headless OFF.
- `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter '^hdr_policy$'`: PASS 1/1 after enabling AIVERSE_BUILD_TESTS; first attempt had no registered tests (exit 8). Report `test-reports/20261120-134245-5185/summary.txt`.
- Contact fixture: Metal high/fixed/medium/low/low-fixed and desktop GL high captures PASS; `python3 tests/render/verify_contact_shadows.py /private/tmp/aiverse-shadow-noise-20261120/captures` PASS (Metal widths 3/11/21, GL 5/12/21, medium 4/11/21). Metal pipelineFailures=0.
- Visual inspection of contact-new.png reveals distinct bands along wide penumbra. User explicitly rejects these bands. This stage is NOT a finished visual fix.
- AIVERSE_TAA_VERIFY=1 did not produce a valid verification despite process exit 0: terrain high MSL compile reports 17 samplers (limit exceeded), no TAA PASS result. Treat as FAIL/unverified, unrelated terrain shader limitation; do not infer success from exit code.
- GPU launches required sandbox escalation for macOS window services. Original sandbox process was terminated.
- Original user foliage scene, Android/WebGL/Vulkan GPU, full repository tests and CI NOT RUN. No push/CI URL.

Next: replace the undersampled static disk with a smooth deterministic spatial filter at bounded cost, inspect wide edges for banding and retain contact hardening. Record any penumbra support limit explicitly; rerun GPU captures, GLSL links and focused runner. Do not alter batching/default experimental flags or unrelated terrain sampler bindings.

## Stage 2: smooth non-temporal penumbra (2026-11-20)

Code: `d43cd1eae3586f171571c377306e4cfdecbdbfc9` on `feature/studio-html5`.
Paths: `engine/render/Shaders.h`, `tests/render/temporal-pcss.md`.
The non-temporal path now uses a deterministic golden-angle disk: 64 taps High, evenly distributed 32-tap subset Medium, normalized squared radial taper. GLSL/MSL match. PCSS radius/blocker search/receiver-plane correction remain; Low and the original temporal disk remain unchanged. No API, uniform layout, save or batching changes.

A texel-dense tent prototype was tested and rejected (not committed): its support cap removed bands but reduced near/middle/far transition widths to 3/4/3 and failed the contact-hardening fixture. Do not resurrect that cap as a fix. The final tapered disk preserves the radius, though its weights produce narrower effective transitions than the old equal-weight disk.

Executed validation on the final source tree (commit performed after testing):
- `python3 tests/render/check_model_lod_shaders.py`: PASS 32 desktop GLSL/GLES variants.
- `cmake --build /private/tmp/aiverse-shadow-noise-20261120 --target aiverse_hdr_scene aiverse_hdr_gl -j 6`: PASS. Log `disk-build.log`; no compiler warning/error matches.
- Metal high/fixed/medium/low/low-fixed and desktop GL high: all six capture processes exit 0. Final images under `disk-captures/`, logs `disk-*.log` in the isolated build.
- `python3 tests/render/verify_contact_shadows.py /private/tmp/aiverse-shadow-noise-20261120/disk-captures`: PASS; Metal 3/6/12, GL 3/7/12, Medium 3/7/12; fixed 18/17/17. Low/fixed remain identical.
- `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter '^hdr_policy$'`: PASS 1/1, report `test-reports/20261120-134822-7875/summary.txt`.
- Viewed full final Metal contact-new.png and enlarged shadow-edge-zoom.png. Pronounced repeated sample bands from Stage 1 are substantially reduced; arbitrary foliage is not verified artifact-free.
- `git diff --check`: PASS. No push or CI dispatched; CI URL/status: N/A.
- Stage 1 TAA fixture terrain-sampler compile failure remains unresolved, not a successful temporal validation. Temporal shader branch is unchanged by Stage 2.
- Original foliage scene, full editor build, full suite, Vulkan/Android/WebGL GPU and GPU performance: NOT RUN.

Risk: non-temporal Medium/High filtering grows from 12/24 to 32/64 bilinear comparisons; thin alpha foliage and extreme softness may still alias. Low cost is unchanged. The user editor binary has NOT been rebuilt/replaced.
Next: rebuild the user's editor on this branch, compare the original tree scene stationary and moving with HDR/TAA off/on, measure target GPU cost; address the separate 17-sampler terrain TAA fixture blocker before claiming temporal end-to-end coverage.

## Stage 3: softer importance-sampled filter (2026-11-20)

Code: 4450a3d41005cd755f20a9e5873f7c1f5eb923a0 on feature/studio-html5. Paths: engine/render/Shaders.h, tests/render/temporal-pcss.md.

User still saw bands in Stage 2. Replace its uniformly distributed disk with importance sampling of density proportional to 1-r², 128/64 equal-contribution taps High/Medium. Table generation is documented in temporal-pcss.md; no per-pixel randomization. This reduces the maximum High tap contribution from roughly 3/64 to 1/128, and broadens the effective kernel relative to the old squared taper. Original PCSS radius, blocker search, receiver correction, Low and temporal paths are retained. Public API, GPU uniform layout and saves unchanged.

Validation on final code tree before commit:
- python3 tests/render/check_model_lod_shaders.py: PASS 32 linked GLSL/GLES variants.
- cmake --build /private/tmp/aiverse-shadow-noise-20261120 --target aiverse_hdr_scene aiverse_hdr_gl -j 6: PASS; importance-build.log, no compiler warning/error matches.
- Six Metal/GL contact capture runs exit 0, files importance-captures/contact-*.ppm and importance-*.log in that build directory.
- python3 tests/render/verify_contact_shadows.py /private/tmp/aiverse-shadow-noise-20261120/importance-captures: PASS. Metal near/middle/far 3/7/14 (Stage 2: 3/6/12), GL 3/8/14, Medium 3/7/13, fixed 21/20/19.
- Visually inspected importance-captures/shadow-edge-zoom.png at the same crop and scale as Stage 2: pronounced sample steps no longer visible, smoother/wider edge. This is a synthetic wall fixture, not original foliage.
- ./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter hdr_policy: PASS 1/1; report test-reports/20261120-135151-11112/summary.txt (actual regex was ^hdr_policy$).
- Direct byte comparison: Low capture identical to Stage 2. GLSL/MSL 128-point table values identical. git diff --check PASS.

Limits: 2x non-temporal filter lookups versus Stage 2, up to 128/64 instead of 64/32, no GPU performance claim. 8-bit output and finite sampling can still band at extreme contrast/zoom; no universal artifact-free claim. Original foliage, full editor, full test suite, Vulkan/Android/WebGL GPU NOT RUN. Earlier independent 17-sampler TAA terrain compile blocker remains. No push/CI dispatched; CI URL/status N/A.
Next: rebuild editor and inspect original foliage at native display scale with camera motion, compare GPU cost on target hardware; if cost or extreme penumbrae remain unacceptable, implement a separate filtered shadow-mask/history path rather than continuing to multiply per-fragment PCF taps.


## Stage 4: material receiver shadow diffusion (2026-11-20)

Code: `9e3f12ddbc09c31f0681f701ace9d396afce82fa` on `feature/studio-html5`.
User requested surface-dependent received-shadow softness as a material parameter.

Added `Material::shadowDiffusion`, default 0, range [0,1] in world units. Model sun-shadow shaders combine its projected radius with the existing PCSS radius in quadrature. Both GLSL/MSL use previously unused emissive.w: GPU uniform size/layout unchanged. Old shadowAt callers retain zero diffusion. Low returns before diffusion; Medium/High receive the effect. Nonzero diffusion avoids the no-blocker early return so nearby shadows can soften across the receiver. No additional filter taps, but this can increase filtering work on otherwise early-returning pixels.

Paths: engine/render/{Model.h,Renderer3DModels.cpp,ModelShaders.h,Shaders.h,GpuMaterialState.h,ModelMerge.cpp,GeometryScene.cpp}; modules/rts/view/RtsLook.cpp; apps/editor/core/MaterialOps.cpp; apps/studio-native/{StudioMaterialValidation.h,StudioPbrMaterialPreview.h,CMakeLists.txt}; apps/studio-web/src/panels/materialSchema.ts; their focused tests, tests/render/{HdrScene.h,model_batching_policy.cpp}; docs/models.md.

Material library load/save, native preview and Studio schema support the optional property; Studio exposes “Shadow diffusion (world units)” using existing shared controls. GPU material hash/equality, merge and palette compatibility include it. Versioned material owners must still advance revisions after mutation. Existing material files default to zero; no gameplay/save-format migration. Public C++ Material has an appended member: rebuild consumers for struct ABI compatibility. No bridge allowlist changes, browser filesystem writes or experimental batching default changes.

Executed checks (isolated Debug build /private/tmp/aiverse-shadow-noise-20261120):
- python3 tests/render/check_model_lod_shaders.py: PASS 32 desktop GLSL/GLES links.
- cmake --build <build> --target aiverse_hdr_scene aiverse_hdr_gl aiverse_model_batching_policy_tests aiverse_studio_material_validation_tests aiverse_studio_pbr_adapter_tests -j6: PASS (material-build.log).
- cmake --build <build> --target aiverse_game_view -j6: PASS (material-view-build.log), including RtsLook loader.
- cmake --build <build> --target aiverse_studio -j6: PASS (material-studio-build.log), full native Studio app with updated web bundle. User's running editor binary was not replaced.
- ./run-tests.sh --no-build --build-dir /private/tmp/aiverse-shadow-noise-20261120 --filter '^(hdr_policy|model_batching_policy|studio_material_validation|studio_material_schema|studio_pbr_adapter)$': PASS 5/5. Report test-reports/20261120-140120-20504/summary.txt. Includes real material library roundtrip, range validation and batching separation.
- Studio npm run typecheck && npm run build && npm run test:web: PASS. After final description edit, repeated typecheck/build/test:materials: PASS, 7 material tests.
- python3 -m unittest discover -s tools/studio -p 'test_*.py': PASS 19/19 (material-ui-contracts.log).
- Apple M3 Ultra Metal and desktop GL captures: six runs exit 0; material-captures/{legacy,zero,fabric,fabric-gl,low-zero,low-fabric}.ppm. AIVERSE_CONTACT_SHADOW_TEST=1; AIVERSE_SHADOW_DIFFUSION_TEST selects PBR receiver and parameter. Fabric uses exaggerated 0.2 for measurement. Metal edge widths zero 3/7/13 versus fabric 9/12/16; GL fabric 9/13/16. Low zero/fabric byte-identical. Legacy mesh byte-identical to Stage 3 importance-captures/contact-new.ppm. Wall, block and lit-floor comparison ROIs have zero changed pixels. Viewed final fabric.png.
- git diff --check: PASS. No push/CI; URL/status N/A.

Limits: artistic receiver filter approximation, not physical fibre scattering. Applies to model received sun shadow maps, not terrain splats, point lights, baked shadows, unlit or nonreceiving materials. Suggested .005–.03 for metre-scale fabrics is tuning guidance, not measured physical data. Native UI interaction/screenshots NOT RUN: CUA failed with native pipe path unavailable; temporary Vite server stopped. Original foliage, full repository suite, performance and other GPU backends NOT RUN. Earlier independent TAA terrain 17-sampler compilation blocker remains; no temporal end-to-end claim.

Studio handoff: docs/handoff/studio-html5/62-material-shadow-diffusion.md.
Next: launch isolated apps/studio-native/AiverseStudioHTML5.app, edit/save/reopen a material and inspect the original scene with stone=0 and fabric around .02; measure GPU cost and verify moving-camera/TAA behavior once the separate sampler blocker is resolved.
