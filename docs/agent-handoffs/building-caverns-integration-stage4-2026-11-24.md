# Building Graph / Caverns integration — Stage 4

Owner: integration/building-caverns-20261124. Code commit: b159de22d.

Fixed the merged Studio request type for `studio.shaderGraphSave`: the guarded save now accepts `expected` and `expectedProject` in the typed bridge declaration. The first `npm run typecheck` on the merged tree failed with TS2353 at `ShaderGraphPanel.tsx:139`; after this one-line fix, `npm run typecheck` exited 0. The worktree used a symlink to the existing `apps/studio-web/node_modules` dependency installation; no dependency versions changed. Native build and full acceptance remain in progress. Do not merge to main before those checks finish.
