# Texture Graph #133 — function authoring regression

Owner branch: `fix/texture-133-function-tests-20261124`, based on PR #133 head `e29e4b6af10ac2c5e8f12f1f197420760f9e8077`.

The current Studio web graph suite failed 2/44. Selecting an unconnected function call as a function body reported a missing input before the intended nested-function rejection. The second test expected a generic budget error for a concrete expanded node-limit rejection. The authoring helper now rejects nested definitions before inspecting their inputs; the test matches the precise node-limit diagnostic. The existing function format and native evaluator are unchanged.

Changed: `apps/studio-web/src/graphs/textureGraphAuthoring.ts`, `apps/studio-web/tests/graphFramework.test.mjs`.

Verification on macOS: `npm run test:graphs` PASS 44/44; `npm run typecheck` PASS; `npm run test:web` PASS (all script groups); `git diff --check` PASS. Studio web dependencies were linked from the existing KINGSTON build, without changing that build. No native build, GPU execution, live Studio, MCP mutation or merged-tree Mac gate was run for this fix. PR #133 still requires its Stage 38CF acceptance; this handoff is not a claim of feature completion.

Next: integrate this small fix into the current PR branch after checking its latest head, then run the Stage 38CF native and Studio acceptance on the exact integrated commit.
