# Region swipe controls

Optional presentation-only input mode in `game.json`, shared by the native client and editor:

```json
"controls": {
  "mode": "regionSwipe",
  "anchorTag": "province",
  "unitTag": "soldier",
  "dispatchAbilities": ["dispatch35", "dispatch50", "dispatch100"],
  "cancelAbility": "return",
  "previewDefence": 1.5,
  "previewNeutralStrength": 0.25
}
```

The map supplies regions. Each region has an entity with `anchorTag`; its owner supplies the
logical owner for input. Its three named point abilities implement dispatch in project Lua.
The shared `RegionSwipe` controller reads the simulation and emits standard ability Commands;
it never modifies units or ownership. The normal simulation validates caster ownership.
Abilities should disable autocast and supply a range covering the map.

A left drag/single-finger swipe starts in an owned region. Crossing owned regions adds sources;
the final region is the destination (except the original source, which cancels). The controller
checks connectivity through owned regions before issuing one ability command per source.
Releasing outside the map cancels. The project must validate the same route in Lua, select
available units, retain a garrison, and schedule departures. Tap near a unit invokes the
anchor's cancel ability at that point; the project resolves the flow and return destination.

HUD ratios are 35/50/100%, default 50%, persisted in `region-ratio.cfg` in the user-data directory.
Raised arrows last 1.4 seconds for locally issued orders. Their wide animated shaft
follows a world-space vertical arch above terrain; a separate translucent silhouette
follows the ground height. Both UI hosts project the same sampled paths, draw shadows
before the continuous ribbon and its moving highlight, and fade the whole effect together. Preview uses the number of units
currently inside source regions and a configurable squared-strength estimate; it is not a
prediction of reinforcements, ongoing damage or scripted reservation queues. Scripts remain
authoritative about the dispatched count. AI/network order arrows are not yet sourced from an
event history. Source highlighting is available in the editor preview.

Orthographic camera starts fitted to the map, with a 1–3x zoom range. Single-finger pan is
suppressed; two-finger zoom/pan is implemented in the client. Wheel zoom preserves the point
under the cursor; double-tapping/clicking water resets fit. Optional automatic frontier framing
is not implemented by this controller. Desktop/editor input was exercised; real-device
multitouch, Android and Web builds were not verified.

Tests: `rts_tests region_swipe` checks multi-source commands, ownership, cancelled releases and
that input preview does not change the simulation checksum. Project tests must check gameplay
selection/dispatch semantics separately.

## Territory information cards

Optional `controls.regionInfo` entries `{ "tag": "mine", "key": "territory.mine" }` match anchor type tags in list order. The client and editor show localized `<key>.title` and `<key>.description` for the region under the pointer or last touch. Entries are presentation-only; gameplay effects stay in game scripts. Editor localization merges engine and project dictionaries. macOS verified; device touch, Android and web remain unverified.

## Camera drag and selected ratio

Right- or middle-mouse dragging pans the ground plane in region mode in both client and editor, independently of unit orders; left drag still dispatches. Zoom does not gate panning. The editor highlights the active percentage; both hosts also display the selected ratio explicitly.

## Dispatch visuals

Shared bounded Bézier strokes render a thicker team-colour arrow with dark outline, filled head and fade-out in both hosts. During a gesture, `RegionSwipe::preview()` ranks physically present soldiers by friendly-cell path distance to the target border, geometric distance, then packed entity handle, matching game dispatch. Highlighted units and displayed count use this same list; it is cached per simulation tick/target/sources/ratio. Preview never mutates simulation state.

### Accepted dispatch orders

`controls.orderTargetCellVariablePrefix` optionally names a trigger-variable prefix.
For each living unit, `<prefix><entity.index>` contains its ordered destination cell
plus one; zero means no active destination. The game publishes these values through
`game.set_var`, clears them on death/completion/cancellation and overwrites reused
entity indices. The UI reads them without changing the simulation.

During a drag, cyan rings mark units already ordered to that destination. Yellow
rings show the next dispatch selection. Existing orders to that destination are
excluded before applying the percentage; the preview count includes new units only.
The game script must apply the same exclusion when executing the command. Orders
to a different destination remain eligible for replacement. Games without this
optional prefix retain their original selection behavior.

### Completed-control flags

`controls.showControlFlags` (default `false`) displays a persistent team-coloured
flag next to each local player's living region anchor, in both editor and client.
Flags use anchor ownership, exactly like dispatch-source eligibility, rather than
painted ground ownership. Occupying part of a neutral/enemy region does not raise a
flag. Flags update on capture/loss, preserve screen size during zoom, leave the
bonus icon visible and do not consume pointer input. macOS verified; Android and
web presentation remain unverified.

Dispatch preview uses a floating card above the pointer: localized caption, large
unit count and selected percentage. The card stays within the viewport; invalid
routes show a dash and the reason, while unfavorable valid dispatches use amber.

### Multiple command anchors in one region

When a region contains multiple living command anchors, RegionSwipe chooses the
local player's anchor; otherwise it keeps the first one in deterministic entity
order. Optional `controls.sharedRegionInfoKey` replaces the information key for
such regions. This enables games to represent independently controlled shares
using ordinary player-owned casters without bypassing command ownership checks.
Control flags follow the player's surviving territory cells when the anchor cell
is lost, and disappear when none remain. The building's position stays unchanged.

### Optional ratio button artwork

`controls.ratioSkin` accepts a project-relative `image` and normalized UV rectangles
`normal` / `selected` (`[u0,v0,u1,v1]`) for the editor's 35/50/100 buttons.
Without artwork the ordinary buttons remain. Game RCSS can use the same atlas
with `.active` to display the selected state. Percent labels remain live text.


### Individual unit swipes

Optional `controls.singleUnitTag`, `singleUnitAbility` and `singleUnitRadius`
(default 1 world unit) enable a separate swipe starting directly on a living
owned unit with the named ability. The nearest eligible unit wins; equal distances
use entity index. This gesture emits one ordinary point-ability command on that
unit, regardless of the region percentage. Crossing owned regions does not add
sources. Its arrow starts at the unit and preview highlights only that unit.
The normal region route check still applies; game scripts validate the actual
physical path. A repeated destination published through the order-variable prefix
is excluded. Death or ownership loss detected during refresh cancels the gesture.
Without these keys the existing region gestures are unchanged. Ground picking is
used; `singleUnitRadius` should match the game's visual footprint.

`region_swipe_single_unit_command` covers caster/command isolation, percentage
independence, multi-source exclusion, accepted-order refresh, enemy units,
out-of-map release, death during drag and fallback to ordinary region dispatch.

### Connected ownership domains

Optional `controls.mergeConnectedTerritory` derives input regions from current
four-neighbor connected territory cells of each non-neutral owner. The immutable
map region grid is never modified. Neutral cells retain their original region IDs.
Source accumulation, frontier distance preview and destination connectivity use the
same derived IDs. One anchor is selected per component, preferring the local owner;
building information still resolves against its original map region. Games must
implement matching connected-source and continuing-attack semantics in their
ability scripts. This option alone changes input, not simulation capture rules.

### Optional single-unit air routes

With `controls.singleUnitAirRoutes: true`, a picked unit whose type has `move.layer: air` may target a land region without a chain of owned ground regions. The unit can also be picked above unassigned/water cells. Invalid/off-map targets, repeated destinations and one-caster command semantics remain unchanged. Default false preserves existing games. This only changes input validation; the ability script remains responsible for simulation flight and destination legality.

### Gesture arrow origin

Region-swipe preview and confirmed arrows start at the actual ground point where each source region was first added to the gesture. Moving within that region does not move the origin. For multi-region gestures each arrow retains its own entry point; individual-unit gestures likewise retain the initial ground click. This changes only presentation, not command casters or destinations.


### Script-defined input groups

Optional `controls.regionGroups: {enabledVar, groupPrefix}` overrides connected-land merging while the enabling simulation variable is positive. Each authored region is keyed by its first row-major cell. `groupPrefix + firstCell` contains a group representative cell plus one; absent/zero/invalid values leave an independent region. Water and out-of-bounds representatives are rejected, as are cross-owner mappings. Disabled/absent configuration preserves existing behavior. This grouping only affects presentation/input and never writes the map.

One gesture produces at most one command per group. Its command caster comes from the authored region where that group's gesture began, preserving per-region elite selection and the original arrow start. Internal regions can therefore share a source while border regions remain independent. This does not itself provide virtual-unit preview counts; games must supply that separately before exposing a virtual-reserve mode.

For virtual reserves, `regionGroups.reservePrefix + groupCell` supplies a non-negative
count (presentation clamps to one million). `originPrefix + groupCell + "_" + targetRegionFirstCell`
supplies the dispatch spawn cell plus one. These entries are read only for active groups;
missing/out-of-range origins suppress that source's preview rather than promising a substitute
physical order. Scripts should cache route publication by topology revision/signature.
`regionGroups.unitTag` identifies physical units eligible across the group; other units
participate only from the authored region where the gesture started.

Preview ranks the reserve spawn as one weighted candidate alongside physical entities,
applies 35/50/100 percent once to the combined count and retains one where possible.
`count()` includes selected virtual soldiers; `preview()` contains only actual entities to
highlight. Existing accepted orders use the authored target region while scripted grouping
is active. Ranking uses friendly-cell distance, nearest authored boundary (including coast),
then entity ID, with the virtual candidate first on a complete tie. Reserve values also
contribute to displayed enemy strength. None of this creates entities or mutates the sim.
