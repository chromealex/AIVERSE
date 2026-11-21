# Lighting Stage 33 — universal entity rule

2026-11-20; integration/lighting-main-stage1-20261120.
Code/documentation commit b77454ce4d6472a98c843f401a1f0bf54d46b614.
User explicitly requires every scene object to be an entity, including lights.
AGENTS.md now requires stable identity, component ownership and common hierarchy,
inspector, viewport selection, transform, history and persistence. Legacy adapters
must retain one canonical owner and cannot be described as a completed migration
merely because they display hierarchy rows. Presentation stays outside authority.

Same block inserted into /Users/aleksandrfeer/Projects/AIVERSE/AGENTS.md for agents
working in the main workspace; existing additional instructions preserved. Root
copy deliberately left uncommitted on its unrelated branch. Lighting branch copy
committed here. No renderer/save format changes; tests NOT RUN (instructions only).
Next: implement document light entity/component adapter with identity and common
editing, retaining existing map compatibility, then verify native/UI behavior.
