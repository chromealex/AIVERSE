# AIVERSE mandatory mcp-contract rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## MCP coverage checks (2026-11-23)

Before using or adding editor/MCP operations, follow `docs/agents/mcp-coverage.md`. Check the built `tools/list` and `mcp_registry` first, run `python3 tools/mcp/audit_registry.py <aiversemcp-binary> --engine <checkout>`, and never confuse registered tools with verified runtime feature parity. Every development stage requires its own `docs/agent-handoffs/` report with actual test evidence and known gaps.
