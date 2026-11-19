---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

# SpectraFX — universal AIVERSE visual effects

**Canonical documentation:** `docs/spectrafx.md`.  
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.  
**Engineering rules:** `AGENTS.md`.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/genre/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `RtsFx` and older `Vfx*` aliases until production parity tests pass.

## Graph editing through MCP (stage 7)

Use the universal editor-core tools when authoring graphs for an open game:

1. `fx_graph_list` and `fx_graph_get {name}` locate and inspect versioned graph assets.
2. `fx_graph_validate {graph|text|name, overrides?}` checks the compiled emitter definition without writing.
3. `fx_graph_set {name, graph|text}` saves only after strict compile validation. Graph names are lowercase `a-z0-9_`; max 48 chars. Invalid sources are not saved.
4. `fx_graph_bake {name, effect, overrides?, overwrite?}` creates `fx/<effect>.json`, consumed by the original CPU renderer and `fx_preview {effect}`. Existing baked effects are protected unless `overwrite:true`.
5. `fx_graph_delete {name}` removes only the graph source; baked effects remain separate.

The MCP commands work in editor and headless `aiversemcp` via `Workspace::call`; they do not depend on RTS or FPS modules. Baked effects do **not** automatically track graph changes. The separate **SpectraFX** window adds a basic graph-v1 canvas, but it is not a full Niagara-equivalent programmable editor.

## Visual editor and ephemeral preview (stage 8, committed but unverified)

- Open **Графика → SpectraFX** in the editor. Select/create a graph, add Spawn/Initialize/Update/Output nodes, drag and connect their pins. Middle mouse pans; the right panel edits properties and exposed parameters as JSON.
- Save calls `fx_graph_set` and reports the exact graph-v1 compiler errors.
- `fx_graph_preview {graph:<JSON>, name?, overrides?, advance?, x?, y?, z?, loop?, freeze?}` is a GUI-only MCP operation. It compiles the **unsaved graph** in memory into the existing CPU particle backend, without creating `fx/*.json`.
- Node `position:[x,y]` is authoring metadata; compilation and runtime effects ignore it. Older graphs without positions remain compatible.
- Check `view_screenshot {window:"scene"}` after a successful preview. All interactive GUI results still require macOS/Windows manual validation; unit-test source presence alone is not verification.

## Typed graph module inspector (stage 9)

The visual **SpectraFX** window now offers a **Типизированные модули**
section per selected node. Use it for common Spawn/Initialize/Update/Output
fields (number, integer, bool, string, choice and Vector3); the editor
validates values against `engine/fx/SpectraModuleSchema.h` and preserves
unrelated node properties. Choose `+` to add a field and `x` to remove
an explicit override. Use **Расширенные свойства (JSON)** for curves,
gradients, nested modules and unsupported fields; a complex value must
**never** be flattened by a typed control.

Native projects may use `spectra::fieldsForNode(type)`,
`spectra::validateTypedField(field,value,error)`,
`spectra::applyTypedField(node,key,value,error)`, and
`spectra::removeTypedField(node,key,error)`, available from
`engine/fx/SpectraFX.h`. The parser `fx::parseEffect` remains the final
source of truth. CTest source was extended but real compilation/tests
have **NOT RUN** in this session. See stage 9 handoff.

## Boundaries and tests

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The current graph compiler produces `fx::EffectDef`; it is not yet a GPU compute graph. Node canvas, graph MCP CRUD, GPU simulation, GPU collision and automatic FPS/RTS production wiring are **unfinished**. Avoid separate genre implementations.

Use CMake target `AIVERSE::spectrafx` in a native project. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every bounded implementation stage must end with a separate dated append to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` in the SAME branch, including implementation/test commit SHA, exact tests executed and their outputs, API/format changes, risks and the next step.
