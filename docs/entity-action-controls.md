# World-anchored entity actions (implementation in progress)

`modules/rts/view/EntityActionControls.h` is a presentation-only model for controls
that belong to a specific entity. The client and editor now share this model, with world-anchored controls rendered
by the game HUD and pointer capture in their region-input paths.

A host supplies an array of definitions with `tag`, point-target entity `ability`,
localized `label`, and `readyPrefix`, `costPrefix`, `remainingPrefix`,
`durationPrefix`, and optional `progressPrefix`. Bindings read deterministic trigger variables whose keys are the
prefix followed by the full packed entity ID (the same integer returned by Lua
`game.units`). Including generation prevents a replacement entity inheriting old
readiness. Costs and remaining/duration values are nonnegative integers; the host
chooses units consistently, e.g. seconds. Missing readiness disables the control.

`refresh` rebuilds owned living sources in stable entity/ability order. `begin`
captures one entity plus ability ID. Refresh cancels the gesture if that source
is lost, dead, missing, disabled, or the viewing player changes. It never changes
to another usable building. `release` returns one ordinary `kCmdAbility` command
only for a dragged, in-bounds world release, then clears capture. UI releases,
clicks without dragging and explicit cancellation emit nothing. Hosts must refresh
before release, capture the pointer, handle focus/Escape/touch cancellation, and
set the issuing player through the normal command pipeline. Simulation remains
responsible for range, target legality, ownership, costs and current readiness.

The manifest opts in with `entityActionControls: [definitions]`. A game HUD can
render `entityActions` rows (`text`, `sub`, `style`, `warn`, `progress`); its elements must use
`pointer-events: none` because native input uses the same screen bounds. Each
120x48 logical-point control sits above its source. Disabled controls still consume
presses. Hosts retain pointer capture after source invalidation until release,
preventing a cancelled gesture from falling through to troop commands.

`view.targeting()` exposes the entity ability ID and packed `caster` during the
drag; player-ability targeting has caster 0. Target legality is game-defined.
Client and editor cancel on Escape, focus loss and source invalidation; the client
also cancels on touch cancellation or a second finger. Touch/device execution,
edge/overlap layout, missile flight/impact visuals and complete B10 styling remain
pending. Do not mark GDD B10 complete on these tests alone.

Validation (2026-10-30): `entity_action_controls_preserve_source_and_cancel_invalid_drags`
passes 37 checks. Full `aiverse_tests`: 5210 checks, with one sandbox loopback-listen
failure in `editor_mcp_ports_are_unique_and_route_to_the_right_listener`; that test
passes all 27 checks when rerun with loopback access. The test target builds with
no source compilation warnings; the existing duplicate-library linker warning
remains. No simulation algorithms, client/editor executable code or shaders are
changed by this isolated header/test addition. Rendering and platform execution
are still unverified until host integration.

Host verification: 41 focused checks pass, including disabled-screen-hit consumption.
Full suite: 5214 checks, only the sandbox loopback-listen failure; its 27 checks pass
with loopback access. Actual macOS client and editor mouse drags launched the free
USA East fixture missile and changed the control to cost 300 / 10-second recharge.
A real client screenshot was inspected at 960x900. The main editor was rebuilt,
restarted, and returned to the live project; MCP 100-tick script check had no errors.
No shaders changed. Mobile hardware, Android and web remain unverified.

Optional progressPrefix reads a fractional Fixed value clamped to [0,1], allowing
a smooth charge display independently of rounded countdown text. Missing bindings
fall back to remaining/duration. Both hosts publish this as entityActions[].progress.
A charged but disabled source says Unavailable instead of Ready (for example when
the player cannot afford it). Games can style ready halos using warn=false without
changing input bounds or simulation state.
