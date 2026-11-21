# AIVERSE HTML5 Studio — Stage 61: correct Python regex escaping in UI contract

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: c626f6f1d9c80325a2bbf184ea120944a70df6ca
Commit: see HEAD.

## Root cause
The Stage 60 Python unittest regex accidentally contained *double backslashes inside a Python raw string*, causing `\\s` to match a literal slash+s rather than whitespace. This was detected by inspecting the exact file committed to GitHub, not by visual inference.

## Fix
- Replaced the regex with `r"export\s+function\s+"+re.escape(component)+r"(?:<[^\n]+>)?\s*\("`.
- It matches both plain `export function StudioButton(...)` and generic `export function StudioTabs<T extends string>(...)` declarations.

## Changed
tools/studio/test_design_system.py
docs/handoff/studio-html5/61-ui-regex-escaping.md

## Validation
- Independently executed the intended Python regex in the local Python 3.13 environment: matched generic StudioTabs correctly.
- GitHub source static checks over current style files: 9/9 passed.
- Full committed repository unittest discovery and npm typecheck/build: NOT RUN in this runtime; pending macOS testing.

## Next
Run `python3 -m unittest discover -s tools/studio -p 'test_*.py' -v` and `cd apps/studio-web && npm run typecheck && npm run build` in a complete AIVERSE worktree. Fix any diagnostics in a new bounded handoff; do not modify main.
