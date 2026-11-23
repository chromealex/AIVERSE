# MCP registry coverage diagnostics — handoff (2026-11-23)

Scope: harden AIVERSE agent introspection and cover a minimal set of cross-subsystem MCP registrations. This stage does **not** implement all engine features as MCP operations.

Branch: `feature/mcp-registry-audit-20261123`; parent: current GitHub `main` at branch creation.
Owner: MCP/editor shared Workspace.

## Changed files

- `apps/editor/core/Workspace.cpp`: registers `mcp_registry` (optional prefix, returns names, input schemas, guiOnly flags, count and guiOnlyCount). Read-only; no document, gameplay or renderer changes.
- `tools/mcp/audit_registry.py`: isolated stdio JSON-RPC handshake, `tools/list`, `tools/call` for `mcp_registry`, required family coverage and cross-check.
- `docs/agents/mcp-coverage.md`: mandatory agent discovery, verification and onboarding workflow.
- `AGENTS.md`: pointer to this contract.

## Verification

Code reviewed against current `Workspace::toolList`, `Workspace::call`, `tools/mcp/main.cpp` dispatch and the existing subsystem registrations on GitHub. **NOT RUN:** local C++ build, standalone MCP smoke script, GUI/Metal testing and full CTest suite (no access to the user's local macOS build environment). Do not describe these as passing.

## Known limitations

- The baseline audit only samples critical tool names; it is not exhaustive feature coverage.
- A name in `tools/list` or `mcp_registry` is not evidence that `tools/call` succeeds in a specific build or editor context.
- GUI-only commands may be registered only by the running editor, not the headless process.
- Generic arbitrary component attach/remove is still missing; this change does not implement that feature.
- Optional STRATA functionality can be compiled out even though capability-inspection tools register.

## Next agent

Run the audit and associated CTests on a fresh built HEAD, inspect GUI-only operations via a running matching editor, extend the baseline from the real feature inventory, and implement missing domain operations with individual end-to-end tests. Document results in a new handoff with exact SHA, platform and test logs.
