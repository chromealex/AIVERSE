---
name: aiverse-mcp-coverage
description: Audit the built AIVERSE MCP tool registry, compare required subsystem capabilities, inspect GUI-only restrictions and verify tools/call against an isolated editor project.
---

# MCP registration and capability audit

Read `docs/agents/mcp-coverage.md` and `tools/mcp/audit_registry.py` before claiming that an engine function is available to AI agents.

## Workflow

1. Sync the intended branch, build `aiversemcp` and rebuild the matching Studio/native editor. A stale executable can give stale tool lists.
2. Run `python3 tools/mcp/audit_registry.py build/aiversemcp --engine .` on the actual binary; check exit status and logged required tool families.
3. Call MCP `tools/list`, then `mcp_registry` (with optional `prefix`) to inspect names, schemas and `guiOnly` metadata. These are registration checks only, **not** functional proof.
4. For every authoring feature being claimed, use an isolated project fixture and invoke its actual `tools/call`. Test valid and invalid parameters, no mutation on failure, read-back, persistence, and Undo/Redo. For GUI-only tools verify the connected running editor, not just local headless fallback.
5. Verify conditional build features against `strata_capabilities` and applicable compile flags. For GPU-backed features require a real platform capture and timing check.
6. Run `python3 tools/skills/catalog.py check` and `python3 tools/skills/audit.py --strict` after editing skills or MCP operations.
7. Record tested SHA, platform, binary path, exact commands and outcomes in a fresh `docs/agent-handoffs/` stage note. Mark unrun checks explicitly.

## Adding missing features

Implement validated, genre-independent Workspace operations in the owner subsystem's Ops files, not inside the JSON-RPC transport. Register from `apps/editor/core/Workspace.cpp`, add a real end-to-end fixture/test and extend `tools/mcp/audit_registry.py` required-family checks. Never claim complete parity from static searches or from a successful `tools/list`.

Workflow over existing MCP and editor code; no runtime library.
