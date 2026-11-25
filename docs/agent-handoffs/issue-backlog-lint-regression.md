# Rules and map view script linting (#165)

Branch codex/issue-backlog-completion, code c9b97d8a9620f3fb900106f82b20d83d63f40f50. The shared lintGameData path used by rules_lint and map_validate skips viewScripts at either document root. A misspelled root key still produces a warning. tests/tests_mech.inc registers rules_lint_recognizes_view_script_roots in the editor-enabled native suite. git diff --check passed before commit. Runtime execution and final Mac quick gate pending. Replay the named regression and check both MCP operations against a disposable game before closing #165.

Ran registered rules_lint_recognizes_view_script_roots body with the actual branch lintGameData, rules loader and simulation on macOS x86_64. Both roots and misspelling assertions passed (../issue-backlog-completion-mac/build-feedback/accept, exit 0).
