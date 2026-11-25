# Selection and targeting in Rules

Implementation 7e2f3869856a8b583d11119c6a2beb36a2106c74. Regression harness 4d7f70271967b175e86c0f431c06e31a84c0a03a adds tests/editor/selection_targeting_mcp.py and its CTest selection_targeting_mcp. The test copies demos/selection-targeting and alternates the authored policy in two headless matches, with rollback on an invalid edit and reopen before the second match.

RulesPanel.tsx, rulesSchema.ts, StudioSession.cpp and types.ts use the existing Rules form and typed studio.rules / studio.rulesSet. The allowlist now admits targeting.acquireOrder alongside unit paths. Unit defaults and inherited values are distinguished on the checkbox. The existing inspector's Edit type in Rules action opens this form.

TypeScript, Vite production build and three rules schema tests passed. An isolated Chrome session opened the production Studio browser preview, entered Logic, and found the Selectable checkbox checked, Auto-target order set to nearest, options Nearest and By map row. This verifies frontend defaults. Native editor persistence is a separate check. Running the new MCP test against the older baseline host reproduced its failure: the host accepted the invalid value and reported both paths as unknown. New binary validation, screenshot and runtime gate pending.
