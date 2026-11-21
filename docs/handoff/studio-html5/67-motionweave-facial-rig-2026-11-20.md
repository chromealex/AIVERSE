# AIVERSE Studio HTML5 — Stage 67 / MotionWeave Stage 181 companion: Facial Rig editor

**Date:** 2026-11-20
**Branch:** `feature/studio-html5`
**Starting Studio HEAD:** `ce8634483b6500eaea045581ba5dda8f00d6d1f7`; parallel Studio UI agents have committed Stage 63–66 during this work. Changes were made through SHA-checked individual file operations, not a force-push or broad merge.
**Separate engine core:** `feature/universal-ecs-animation-pipeline` HEAD at start `a826757ea1ffb994b39ad50c852811f13c8cf5f0` (FacialControlRig Stage 180). These branches **are divergent and were not merged**.
**Target:** real React/HTML5 Studio under its first-approved cinematic style. No feature edits to legacy ImGui. No fake native GPU live preview. Mandatory handoff and truthful native test status.

## Source/code commits and scope

| Commit prefix | Changed path | Purpose |
|---|---|---|
| `746ef806`, `9f2f2e81`, `a3c517bd`, `2be174b7`, `86c5c788` | `apps/studio-native/StudioFacialRigDocument.h` | New bounded project-local Facial Rig v1 schema validation, listing/read/save, explicit sanitized file names, no-follow descriptor-relative exclusive staging, fsync, same-directory publish; syntax/portability corrections |
| `37ab50a2` | `apps/studio-native/StudioSession.cpp` | Strict native bridge whitelist for `studio.facialRigList/Get/Validate/Save`; returns runtimeVerified=false |
| `6cad1c5b`, `f8d83ccd` | `apps/studio-web/src/panels/facialRigSchema.ts` | Typed Stage 180-compatible JSON v1 morph+IK DTO, explicit ARKit 52 semantic names, pure client-side bounded validation |
| `d5ee8d3b` | `apps/studio-web/src/types.ts` | Four typed method params/results, no arbitrary path or untyped access |
| `ea740657` | `apps/studio-web/src/mock.ts` | Browser preview has honest empty catalog; native-only writes/get/validation reject |
| `9eea4e7e`, `379e9b49`, `c393647b`, `017e9008` | `apps/studio-web/src/panels/FacialRigPanel.tsx` | New standalone UI-kit-driven Facial Rig panel with 52-morph template, search, dedicated editor, per-channel gain sliders, optional eye Aim authoring, schema validate, save/discard and errors |
| `4cd0ed4c`, `d4c48263`, `6862add4` | `FacialRigPanel.css` | Narrow-dock-safe layout using existing `--au-*` semantic tokens; no new palette, no viewport manipulation |
| `c625a8ec`, `1cc0c030` | `apps/studio-web/src/panels/StudioPanelRenderer.tsx` | Imported and registered Facial Rig; kept original Animation timeline placeholder intact |
| `2deb5506` | `apps/studio-web/src/nestedDock.ts` | Separate typed `facial_rig` dock tab; existing Animation, Scene and Game tabs retained |
| `1314017f`, `03472b98` | `apps/studio-native/tests/StudioFacialRigDocumentTests.cpp` | Release-on assertions for schema, duplicate paths, read/list/save and temporary-file roundtrip with PID-scoped test folder |
| `8074853c` | `apps/studio-native/CMakeLists.txt` | New native `aiverse_studio_facial_rig_tests` / `studio_facial_rig_document` target |
| `4f4dbea0` | `apps/studio-web/tests/facialRigSchema.test.mjs` | 52-name uniqueness, schema rejection, path validation, honest mock-only behavior |
| `f35d397e` | `apps/studio-web/package.json` | Adds `test:facial` to `test:web` |
| `061683ce` | `docs/studio-facial-rig-authoring.md` | Native schema/authorization, UI user flow, integration requirements and exact limitations |

## API/data/ownership

- Project asset file is `<trusted-project>/facial-rigs/<validated-name>.json`, not a browser-controlled filesystem path. The C++ validator accepts the Stage 180 JSON v1 `{version:1,morphs:[{parameter,node,target,gain,bias,halfLife,blend}],rig:[...]}` structure, bounded to 256 mapping entries, 64 existing AnimationRig operator entries and a total 128KiB document.
- The UI uses only `StudioPanel`, `StudioSection`, `StudioField`, `StudioButton`, `StudioInput`, `StudioSelect`, `StudioTabs`, `StudioBadge`, `StudioAlert`, `StudioEmptyState` from the central UI kit. Existing dark cinematic tokens are reused; no new token values or native Metal rect changes.
- Native `StudioSession` explicitly whitelists `studio.facialRigList/Get/Validate/Save`. Read/list/save require an open Workspace project. Native validation rejects malformed payloads and unsafe names. File saving does **not** use MapDoc Undo, which the panel states clearly.
- The 52 names are artist-facing optional mapping suggestions. Importer Model compatibility, morph name existence, facial IK target compile and GPU rendering are **NOT** checked by Studio. The companion MotionWeave Stage 180 `FacialControlRig` will compile these after branches merge; maintain exactly one pose owner.
- Browser demo has no fake project or GPU preview. Studio's existing native Metal Renderer3D Scene is unchanged, and no live facial rigging can be shown until both branches merge and a native Model/presentation source is bound. The UI visibly states this.

## Tests: performed versus NOT RUN

**ACTUAL:** authenticated GitHub source audit of Studio panel registry, nested dock, typed bridge and native StudioSession; review of design system/agent conventions; authored SHA-guarded code, backend, test, docs commits. Readback confirmed ARKit template has exactly 52 **unique** names and that the four method contracts appear in source. Compiler/API readback was performed, not execution. Other Studio agents' concurrent commits were preserved.

**NOT RUN:** `npm run typecheck`, `npm run build`, `npm run test:web`, `npm run test:facial`, CMake/native compiler/CTest, macOS WKWebView/capture/live native GPU preview, Metal/Vulkan/GLES parity, sanitizer or animation/crowd benchmark. **No pass claims.** Editor UX, the typed bridge and C++ file I/O remain unverified until the owner's Mac compiles/runs them.

## Next concrete steps

1. Run frontend strict build and new native Studio facial test on Mac, fix any compiler/type/runtime errors immediately; test 1440×900 and narrow 240px panel. Verify persistence when switching real project directories.
2. Merge `feature/universal-ecs-animation-pipeline` into an integration branch that includes `feature/studio-html5` with semantic conflict resolution. Preserve both native device/scene and HTML5 UI event flows; do **not** push the cross-merge directly to main.
3. Add exact imported Model/Graph selection and a whitelisted **real** facial preview bridge (pose generation and identity, 52 morph sliders feeding `FacialControlRig`, native Renderer3D draw). Never render a synthetic face or mutate authoritative gameplay ECS.
4. Finish automatic morph-preserving LOD geometry and the requested one-pass full Mac test campaign. PR #22 remains draft/unmerged until validated.

**Blocked intentionally:** live 3D facial preview and named morph bone validation cannot truthfully be declared implemented on `studio-html5` before source/runtime integration. Do not silently replace this with web canvas mock rendering or duplicate the MotionWeave runtime algorithms.

**Follow-up edit:** `017e9008` removes automatic `workspace.changed` refresh while the facial panel is mounted; it prevented unrelated editor changes from silently dropping an unsaved facial draft. Manual refresh remains disabled while dirty. Native CMake/CTest and web typecheck remain **NOT RUN**.
