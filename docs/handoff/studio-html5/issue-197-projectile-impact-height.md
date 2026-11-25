# Projectile impact height in Rules

Feature branch feature/aiv197_projectile_height, implementation 9c280af93708b77c8fbfb1961d9b4c746b82e50c, current range content ea3f3150d00774ac5c17ac98e991729084fd6a1a.

The Rules panel renders the target unit's optional view.impactHeight in Appearance. The Inspector's existing Edit type in Rules command opens the same form. It uses RuleRow and studio.rulesSet. Clearing an explicit height deletes the property and allows an inherited value. No additional bridge method or native file writer is required.

- Source: apps/studio-web/src/panels/rulesSchema.ts and apps/studio-web/tests/rulesSchema.test.mjs. Related implementation, demo, runtime limits and native gate are recorded in docs/agent-handoffs/projectile-impact-height-initial.md.
- node --experimental-strip-types --test apps/studio-web/tests/rulesSchema.test.mjs passed all three tests, including entering zero on an unconfigured target, rejecting negative input and clearing the override.
- npm --prefix apps/studio-web run build -- --outDir /Volumes/KINGSTON/AIVERSE-builds/projectile-197-mac/studio-web-dist passed TypeScript and Vite production compilation of 1836 modules, with Vite's existing chunk-size notice.
- Native StudioSession probe linked to the existing Mac Studio test objects and opened a disposable copy of the range. studio.rulesSet changed types.low.view.impactHeight from 0.5 to 1.25. studio.projectHistoryStep undo restored 0.5, redo restored 1.25, and reopening the project in a new StudioSession read 1.25. Reusable probe executable: /Volumes/KINGSTON/AIVERSE-builds/projectile-197-mac/authoring-test/studio197 <engine-root> <disposable-project>.
- The standalone MCP server also round-tripped this field and preserved its sibling model path, recorded in docs/evidence/projectile-impact-height-authoring.json. Registry audit reported no missing feature tools.
- The optimized arm64 interpolation probe processed one million varying projectiles in 0.001518 seconds on this machine. Process peak RSS was 1,277,952 bytes. This measures the pure allocation-free projection, excluding model draws, scene extraction, particle updates and GPU cost.

The TypeScript schema was verified against real native save/undo/redo semantics independently of the renderer. The main handoff tracks native/Web frame evidence and integration.
