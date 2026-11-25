---
name: aiverse-fx
description: Create and attach AIVERSE visual effects through MCP - fx templates or a Unity prefab import, tuning, binding to game events, preview and screenshots.
---

# Visual effects

1. Start from `fx_templates` (explosion, fire, smoke, magic, weather, ...) or `fx_import_unity {source: .prefab or folder}` to rebuild a Unity effect. Format: `docs/particles.md`.
2. Tune with `fx_get` / `fx_set`; list with `fx_list`; remove with `fx_delete`.
3. Look: `fx_preview {effect, advance, freeze}` + `view_screenshot`; check at gameplay zoom, not only close up.
4. Bind to events with `fx_bind` (died, hit, attack, ability, spawned - per type, weapon or ability).
   For chain lightning use `fx_bind` with `cue:"chainJump", ability:"lightning", mode:"beam", lifetime:0.2`. An explicit `chainKey` selects a chain effect’s `chainFx` override. For a travelling effect use `mode:"projectile"` and an effect name.
   For named event anchors use `node`, `offset`, `inheritRotation`, `inheritScale`, and (attack/ability) `anchor:"target"` in fx_bind. Missing nodes fall back to height placement; do not assume GPU ECS attachments resolve these names.
5. Cost: keep particle counts modest for phones; compare a heavy battle on `view_postfx {quality: low}` and a low-end `device_set` profile.
   Read the binding bank with `fx_get {name:"fx"}`. Preserve existing bindings when setting `limits.combatPerFrame` and `limits.particles.{low,medium,high}` via `fx_set`. Inspect `skippedCombatEvents` in a Game-view `fx_preview` response.
6. Effects are presentation only - never drive gameplay from them.

Runtime library: none. Workflow over the engine MCP fx tools.
