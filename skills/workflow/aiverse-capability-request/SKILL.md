---
name: aiverse-capability-request
description: When building a project with AIVERSE, identify verified missing engine capabilities and offer to send a minimal feature request to the engine maintainers.
---

# Report a missing engine capability

Use this when a user's project request exposes a capability AIVERSE does not yet provide. This workflow has no runtime library.

1. Run the capability audit in `skills/workflow/aiverse-capability-audit/SKILL.md` against the current engine checkout. Search `docs/capability-status.json`, the skill and library catalogs, relevant docs, and code. A missing example, adapter, or undocumented API is not automatically a missing engine capability.
2. Keep building the user's project with available capabilities. Do not turn the report into a prerequisite for their work.
3. For a verified gap, write a small JSON draft containing `title`, `need`, `gap`, `existing`, `evidence`, and optionally `area`. Describe reusable engine behavior, not a genre-specific feature. `evidence` should cite public engine paths or capability IDs. Do not include project names, private source, prompts, assets, credentials, or personal details.
4. Preview the exact public issue text with `python3 tools/feedback/capability_request.py preview draft.json`. Show it to the user and ask whether to send it. Never infer consent from the user's request to build a project. If the user declines or does not answer, stop here.
5. After explicit consent, run `python3 tools/feedback/capability_request.py submit draft.json --consent`. The tool checks for a matching open issue before creation. If it finds one, show its URL to the user and do not post a duplicate. If GitHub authentication is unavailable, leave the draft local and explain how to submit it later. Never add a comment or upload project files automatically.

The destination is the public `chromealex/AIVERSE` GitHub Issues tracker. Tell the user this before requesting consent. The CLI validates and bounds every field. Keep one request per distinct capability; do not submit speculative ideas as confirmed gaps.
