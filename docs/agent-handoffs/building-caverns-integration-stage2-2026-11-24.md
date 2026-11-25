# Building Graph / Caverns integration — Stage 2

Owner: integration/building-caverns-20261124. Code commit: 623df3ce9.

Applied the exact Stage 53-54 source delta from `0eae050b9aff2185dbef6d34e6833fba45d9268c..809022386` onto the Building staging tree. The full source branch contained unrelated earlier history and produced 12 conflicts, so that full merge was aborted. The scoped delta changed graph transaction, shader/FX owner operations, native and browser original-document guards, planetary read-only projection, related docs and tests. Four patch conflicts were resolved against the newer Building tree; `types.ts` retained its `expectedMap` support, and the graph test gained the shader create/stale/busy fixture. Existing feature manifests were retained and the graph manifest marked merged-tree acceptance pending.

Verification on code commit `623df3ce9`: `git diff --check` exit 0 before commit; `node --experimental-strip-types --test apps/studio-web/tests/caveGraphInspection.test.mjs` exit 0, 5/5 PASS. `npm run typecheck` exit 127 because `tsc` is not installed in this isolated worktree. Native graph tests, live MCP, full Studio build, visual QA and merged-tree Mac quick gate are **NOT RUN**. Feature Gate remains incomplete; do not merge to main.

Next: reconcile current Caverns owner and the Stage 55 inspection mount, then run the native and Studio acceptance on the final merged tree. Preserve source branches and record any further test failures separately.
