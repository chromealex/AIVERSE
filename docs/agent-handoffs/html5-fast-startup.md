# HTML5 fast startup policy

2026-11-26. Policy commit `ec8a2b329dc8e15ab6e8f1d0872d5078ec68ec7b` routes HTML5 exports from `AGENTS.md` to the browser acceptance procedure in `docs/agents/contracts/build-testing.md`.

Verified contract routing with `python3.12 tools/agents/check_contracts.py` (zero errors) and whitespace with `git -c core.whitespace=cr-at-eol diff --check`. Python 3.8 lacks the existing checker's Path.is_relative_to API. This documentation change adds no runtime code. The merged-tree Mac quick gate is pending integration.
