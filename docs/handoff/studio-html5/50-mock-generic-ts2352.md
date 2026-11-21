# AIVERSE HTML5 Studio — Stage 50: fix generic mockInvoke return types (TS2352)

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: 3dacb52fbe0c08fd4bc743a896dcca2b7012cfcd
Commit SHA: see this stage in branch history.

## User-reported diagnostics
Running `npm run typecheck` on the developer's Mac produced four TS2352 errors in apps/studio-web/src/mock.ts, lines 35, 36, 47, 55. The errors involved studio.shaderGraphCatalog, studio.shaderGraphs, studio.lighting, and studio.assets.

## Root cause
`mockInvoke<M extends StudioMethod>` returned values using assertions `as StudioResult[M]`. Generic type parameter M is not narrowed when the implementation compares `method === "studio.foo"`, so `StudioResult[M]` could describe any method-specific return shape. TypeScript's TS2352 diagnostics were valid.

## Implemented
- Public overload retains the exact `mockInvoke<M extends StudioMethod>(method: M, params: StudioParams[M]): Promise<StudioResult[M]>` API, preserving method/params/response inference at callers such as bridge.ts.
- Non-generic, union-typed implementation `mockInvoke(method: StudioMethod, params: StudioParams[StudioMethod]): Promise<StudioResult[StudioMethod]>` allows TypeScript to validate each return against the valid union of DTOs.
- Removed every unchecked `as StudioResult[M]` assertion, including the original four failures. Return payloads and DEMO behavior are unchanged.
- Added runtime contract tests for snapshot, Shader Graph, lighting, assets, materials, viewport and native-only operation rejection.
- Registered `npm run test:mock` in `test:web`, which is run from tools/studio/verify_headless.sh and CI.

## Files
apps/studio-web/src/mock.ts
apps/studio-web/tests/mock.test.mjs
apps/studio-web/package.json
docs/handoff/studio-html5/50-mock-generic-ts2352.md

## Tests actually executed
- On Node 22.16.0 / TypeScript 5.8.3, a local reconstructed type-check fixture containing the full mock.ts and types.ts and their imported pure model types passed `tsc --noEmit --strict --noUnusedLocals --noUnusedParameters` (0 errors).
- On Node 22.16.0 the new mock.test.mjs passed **5/5**, no failures.
- Full `npm run typecheck`, Vite build, and macOS Metal/WebView build NOT RUN in the local reconstruction (the full repo and all npm dependencies were unavailable). These still need verification on the developer Mac.

## Next
In the developer worktree: `git fetch origin && git switch feature/studio-html5 && git pull --ff-only origin feature/studio-html5 && cd apps/studio-web && npm run typecheck && npm run test:mock && npm run build`.
Then fix any additional diagnostics based on the real output; use `bash tools/studio/verify_mac.sh` on macOS once the frontend is green. Preserve the legacy ImGui fallback and handoff per stage.
