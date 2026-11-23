# MCP coverage contract for AIVERSE agents

Updated: 2026-11-23. Source of truth: `Workspace::ops()` in `apps/editor/core/Workspace.cpp`; `aiversemcp` exposes it via `tools/list`.

## Before any authoring task

1. Sync the required branch and **rebuild both** `aiversemcp` and the editor/Studio host. An old server or editor can hide or reject new tools.
2. Call `tools/list`, then `mcp_registry`. The latter exposes `guiOnly` and exact input schemas. Use a prefix (e.g. `fx_graph_`) to inspect one family.
3. Never assume that a subsystem, editor window, skill, or C++ class has a corresponding MCP operation. Search the actual names. A returned tool name proves registration only; it does **not** prove that the corresponding feature is operational.
4. For a mutating operation, call `tools/call` on an isolated fixture. Verify changes using its read-back tool, save/reopen, Undo/Redo where applicable, and the matching CTest. GPU features need GPU evidence and an actual Studio visual check.
5. GUI-only commands require a running, compatible editor. Headless local fallback uses `Workspace::call` and may not have the GUI operation registered. Never treat fallback as a successful editor mutation.
6. Do not silently substitute a generic JSON patch for a domain API. Apply the same validation, edit history, persistence, cross-project and genre-neutral constraints as the native editor.
7. Every change must include a handoff under `docs/agent-handoffs/`: owner, branch/SHA, exact changed files, intended behavior, actual tests (distinguishing NOT RUN from PASS), known gaps and next step.

## Running the audit

```sh
cmake --build build --target aiversemcp
python3 tools/mcp/audit_registry.py build/aiversemcp --engine .
python3 tools/skills/audit.py --strict
```

`audit_registry.py` launches a real standalone MCP process with `--no-editor --port 0`, checks `initialize`, `tools/list`, and the `mcp_registry` call, comparing the reported names, counts and `guiOnly` metadata. Required families are a **minimum smoke baseline**. A passing result does not mean every engine feature is exposed or tested.

## Known coverage gaps / do not claim complete parity

- Generic attach/remove of arbitrary ECS components on document scene entities is not available from the current `scene_component_set` patch API; implement explicit validated operations rather than guessing JSON fields.
- HTML5 Studio panels and the native ImGui editor are not feature-parity proof: `docs/studio-html5-parity.md` tracks missing previews, visual workflows and unfinished GUI behaviors.
- Compile-time optional operations (e.g. STRATA builder and battle networking), platform GPU dependencies, loaded project, and editor compatibility must be validated at runtime.
- `tools/list` describes the **local workspace** registry; GUI-only commands registered by a live editor are not necessarily in that list. Verify the editor transport and version explicitly.

## How to expose the next missing feature

- Prefer an operation in `apps/editor/core/<Subsystem>Ops.cpp` registered by `Workspace`, with tight JSON input schema, project-aware path validation, proper revisions, Undo/Redo and save semantics.
- Do not put a renderer- or platform-specific behavior in the generic MCP transport (`tools/mcp/main.cpp`); keep transport as JSON-RPC forwarding and the feature in its owning subsystem.
- Add a test that uses the **built** `aiversemcp` and calls the operation end to end; test error paths, no mutation on failure, read-back, and GUI integration if GUI-only.
- Update the family baseline in `tools/mcp/audit_registry.py`, the owning skill/doc, and a handoff. Record any unimplemented capability explicitly.
