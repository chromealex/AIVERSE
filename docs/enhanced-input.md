# AIVerse InputFlow — action-based input for all game genres

**AIVerse InputFlow** (short: **InputFlow**) is AIVERSE's genre-independent, action-based **presentation input** system. It uses a device → mapping context → action → trigger pipeline, inspired by Unreal Engine's Enhanced Input without copying its C++ API or binary asset format.

The canonical native C++ header is `#include "engine/platform/input/InputFlowCore.h"`. Use `aiverse::lib::gameplay::inputflow::InputFlow` to create the runtime and `inputflow::InputAction`, `inputflow::InputContext` and `inputflow::InputValue` to describe input. The old `"engine/platform/input/EnhancedInputCore.h"` and `aiverse::lib::gameplay::enhanced::InputSystem` remain source-compatible aliases. Existing `input/actions.json` paths, profile file names and command layouts remain unchanged. New native consumers should link `AIVERSE::InputFlow` (or `AIVERSE::lib_inputflow`); `AIVERSE::lib_input` is preserved as an alias to the same header-only library.

## Runtime pipeline

\`\`\`
platform::Event / GamepadState
  -> platform::EnhancedInputAdapter (keyboard, pointer, wheel, touch, pads)
  -> lib::gameplay::inputflow::InputFlow
       active Mapping Contexts, sorted by priority
       physical Control -> mapping Modifiers -> mapping Triggers
       Action Value aggregation -> action Modifiers -> action Triggers
       Started / Ongoing / Triggered / Completed / Canceled
  -> gameplay presentation code and UI
  -> validated / tick-stamped integer or Fixed Commands
  -> ECS systems / authoritative simulation
\`\`\`

Input processing is **not** ECS authoritative simulation. Never feed float device state directly to deterministic ECS. ECS systems consume semantic commands and own the rules; UI and remapping stay local. A single \`Fire\` action can be converted into FPS, RTS, vehicle, turret or UI commands without duplicating physical-key handling.

Files:
- \`engine/gameplay/Input.h\`: legacy \`InputPhase\`, \`Binding\`, \`resolveInput\`, \`deadzone\` and selection helpers (unchanged).
- \`engine/platform/input/EnhancedInputCore.h\`: header-only pure C++20 core, target \`AIVERSE::lib_input\` / native \`aiverse_native_use_libraries(target input)\`.
- \`engine/platform/EnhancedInputAdapter.h\`: OS event and gamepad conversion; requires platform hosting.
- \`engine/platform/EnhancedInputConfig.h\`: strict JSON action assets and per-user rebinding profiles; requires the engine \`Json\` type, so native modules link engine interfaces if they need it.
- \`apps/fps/client.cpp\`: FPS reference use; maps action state to the unchanged \`fps::Input\` command.
- \`docs/enhanced-input-agent-handoff.md\`: stage-by-stage implementation and continuation record; **read this before modifying**.

## Unified conditional consumption and deep chord dependencies (Stage 54)

Mapping **and Action-level** `Chord`/`NotChord` gates now participate in
the same effective-control preview and priority arbitration. A high-priority
Action that is disabled by its own Action-level chord no longer blocks a
lower-priority mapping of the same physical control. Both the recursive
Action resolver and the main event-dispatch pass use identical chord
eligibility for Mapping consumption.

Conditional refinement is no longer artificially limited to eight passes:
the bound scales with the number of Actions (capped at 512 to prevent
unbounded work for malformed native configurations), with deterministic
fallback on a repeated/non-convergent state. A normal acyclic Action
prerequisite chain can therefore exceed eight levels without a declaration
order dependency. Cyclic JSON/native graphs remain rejected.

The dedicated core tests for Action-level priority fallback and a
12-Action positive chain are authored, **not executed**. Performance under
thousands of live conditionally consuming mappings and native compiler
behavior still need measured validation before release.

## Direct editor GameWindow pointer Action parity (Stage 53)

The editor's **direct RTS play-test** now samples keyboard, supported mouse
buttons, mouse X/Y motion and horizontal/vertical wheel channels into its
own InputFlow each frame **before** script-card and viewport interactions.
It de-duplicates repeated controls across contexts and cancels its
presentation Action state when Game loses focus or text input takes ownership.
Hosted-client mode remains separate: its `ClientHost` continues forwarding
platform events to the game's own InputFlow and must not double-sample
editor pointer input.

`GameWindow` uses `actionDefinition(name)` (a new read-only public
`InputFlow` getter) and mapping trigger declarations to determine whether
a mouse Action should take effect on the first physical click or on a
semantic `Triggered` event. This supports remapped Select/Command with
Pressed/Tap/Hold/DoubleTap/Pulse and Chord/NotChord gates, while drag
completion continues to follow the physical button's release. It also
reads the authored `CameraZoom` Action for wheel scaling/rebinding rather
than bypassing it with raw `ImGuiIO::MouseWheel`.

Bindings to *non-mouse* devices are intentionally not converted into fake
cursor clicks; Action semantics require a meaningful pointer location for
selection, placement and drag. GameWindow still relies on ImGui's pointer
ownership and world picking and sends the same validated RTS Commands
rather than writing raw input to ECS.

The core `actionDefinition` unit assertion is authored. Direct GameWindow
ImGui behavior (script card, box select, rebinding and wheel/pinch modes)
has **not yet been compiled or manually exercised**; these remain explicit
release validation tasks.

## RTS UI capture and legacy presentation key mirrors (Stage 52)

`ClientInputRouting::onEvent(e, gameplayAllowed=false)` now cancels the
keyboard, pointer or touch device **corresponding to the captured event**,
rather than treating every RmlUi pointer interaction as an OS FocusLost.
It also advances `InputFlow::update(0)` to settle cancellation phases
without dispatching captured input to gameplay. Holding keyboard camera
movement while clicking an unrelated HUD panel therefore retains the
keyboard Action; text/modal capture still suppresses gameplay keys.

The RTS `GameClient` keeps a historical `keys_` array solely for custom
fallbacks such as project-owned top-down/possession controls. That array now
ignores KeyDown captured by RmlUi, text fields and how-to modals, and clears
all bits when gameplay keyboard input is disabled for a presentation frame.
Semantic InputFlow Actions remain the preferred path; never feed
`keys_` directly into deterministic ECS simulation.

The focused `enhanced_rts_input_tests.cpp` scenario is authored but not
executed. Validate real editor-hosted focus and text input in the later
GUI smoke pass. The separate direct editor GameWindow still uses
physical mouse-button lookup for Select/Command and does not execute all
advanced mapping triggers; see the Stage 53 follow-up in the handoff.

## No phantom input edges on context activation (Stage 51)

When a Mapping Context is enabled or its bindings are replaced **after** a
physical key was already pressed, InputFlow marks the affected Mapping and
Action runtimes as `suppressUntilRelease`. Until the actual key goes up,
edge and timed triggers (`Pressed`, `Released`, `Tap`, `DoubleTap`,
`Hold`, `HoldAndRelease`, `Pulse`) do not treat that old hold as a new
interaction. Continuous input (`Down`, pure `Chord`/`NotChord`, or no
mapping triggers) remains readable, allowing gameplay to resume movement
with an already held key when the owning context returns.

Once the physical source releases, the suppression flag clears and the next
genuine KeyDown works normally. This applies to both Mapping triggers and
Action triggers; it prevents a newly opened modal from accidentally
accepting a button that originally opened the modal. Global FocusLost
still clears all raw controls; the host must not replay stale KeyDown
events on FocusGained.

The additional same-frame activation cases in
`enhanced_input_tests.cpp` are authored but not executed. Backend
event ordering and GUI input capture require later native smoke tests.

## Multi-local-user device ownership and scoped cancellation (Stage 50)

`InputFlow::cancelDevices({Device::Keyboard, ...}, deviceIndex = -1)`
removes only the listed device families from the raw input buffer and resets
their Mapping runtimes. It leaves other owned device channels untouched. A
subsequent `update(0)` or the next normal presentation update emits
`Canceled` when an affected Action no longer has a live source; an Action
also mapped to an unaffected held device remains active. Unlike global
`cancelAll()`, device cancellation is **deferred to the next update**, not
an immediate Action event.

`platform::InputFlowAdapter` exposes `cancelKeyboard()`,
`cancelPointer()`, and `cancelTouch()` (which also clears tracked fingers
and gesture baselines). `InputFlowUsers::setKeyboardOwner`,
`setPointerOwner`, `setTouchOwner`, and `assignGamepad` now cancel only
the transferred device class, preserving unrelated held actions from a
different controller, keyboard or pointer. Owners can be unassigned with
`clearKeyboardOwner`, `clearPointerOwner`, and `clearTouchOwner`. A UI
capture event `onEvent(e, false)` cancels only the captured event's class,
while `FocusLost` still globally cancels all devices.

`InputFlowUsers::pollGamepads()` remains one physical poll per frame with
per-user logical pad index 0. `InputFlowAdapter::pollGamepads()` is for
single-user clients and must not be called on the same hardware frame as
the multi-user router. Local users may have independent mapping schemes and
profiles but still share one presentation input backend.

Stage 50 routing tests are authored, **not yet executed**. Device removal
is intentionally a presentation-layer operation, never an ECS state write.

## Action lifetime across focus, gating and context changes (Stage 49)

An Action's `active` flag now reflects its **effective** Action-level
`Chord`/`NotChord` eligibility, not just the unmodified physical key. When
a required prerequisite disappears, the Action returns a zero value and emits
`Canceled` rather than remaining active until the held key is released.
Mappings interrupted by a higher-priority consuming context likewise cancel
their existing Action. Ordinary physical release of a successfully triggered
Action still completes normally.

A blocked Mapping or failed Mapping/Action chord gate clears its Hold/Pulse/
DoubleTap timing history. Restoring the chord while the key stays physically
down restarts a timed Hold from the newly eligible interval; it does not
inherit time from behind a modal. When a context is activated or a mapping
is replaced while a key is already held, InputFlow initializes its mapping
history from the current persistent control sample instead of inventing a
new physical key-down transition. FocusLost through InputFlowAdapter still
uses `cancelAll()` and clears all raw controls.

The new lifecycle regression tests are authored but **not yet executed**.
Interaction edges that occur *before the very first update of a newly
activated context* still depend on host event ordering and must be
verified in the macOS/Windows editor smoke phase.

## Chord dependencies and deterministic evaluation (Stage 47)

`Chord("Aim")` and `NotChord("Aim")` are **level predicates** on an effective
Action, not simple tests of whether the underlying key is physically down. On
every presentation update, InputFlow takes a non-mutating control snapshot in
Mapping Context priority order, applies mapping modifiers, then resolves Action
modifiers, ValueType constraints and transitive chord dependencies. For example,
if `Finisher` requires `Stance` and `Stance` requires `Shift`, holding the
Stance control without Shift cannot activate Finisher, even if Finisher's control
was pressed in the same frame. The evaluation does not depend on Action names
or declaration order.

**Cycles are invalid**, including loops involving `NotChord`. JSON v1 assets
reject direct and transitive dependency cycles without modifying the loaded
configuration; the native `InputFlow::defineAction` and
`InputFlow::registerContext` APIs likewise reject a proposed cycle before
changing the live Action/Context. Forward references to Actions are allowed
during native definitions, and JSON validates them after reading the complete
asset. Temporal trigger conditions (`Tap`, `Hold`, `Pressed`, etc.) still
control phase/events; a held Action need not emit `Triggered` in the current
frame to satisfy a chord's level predicate.

The first snapshot respects context priorities and consumption. Starting in
Stage 48, InputFlow refines that snapshot against the effective chord gates:
a higher-priority mapping whose `Chord` condition is false no longer hides a
lower-priority binding of the same key. Chained prerequisites can then resolve
from the lower Action in the same presentation frame. This is a bounded
fixed-point refinement (up to the declared Action count plus one, capped at
512 passes for pathological native graphs); when mutually dependent
priority/consumption patterns have no consistent fixed point, it
deterministically falls back to the conservative original priority snapshot.
For intentional simultaneous sharing, prefer `consume=false`; do not rely on
feedback cycles where a high-priority binding consumes the very control needed
by its own prerequisite in a lower context. Action-level timing gates still
control phases rather than the mapping's consumption policy.

Stage 47–48 tests are authored, but compilation and runtime execution have
**not** yet been confirmed.

## Stage 56 — validation evidence and remaining release gate

The implementation branch has now entered the validation phase. At commit
`cf26d02d0343`, GitHub Actions scheduled targeted InputFlow builds on
Linux, macOS and Windows; **those jobs were still queued when inspected**,
so they do not establish a PASS.

One *full CI* Linux ARM64 job reached compilation and failed in unrelated
ECS regression tests, because `assert(...)` checks in
`tests/ecs_wave_executor_test.cpp` and
`tests/ecs_experiments_test.cpp` disappeared in Release with `-DNDEBUG`,
leaving check-only variables unused under `-Werror`. Stage 56 replaced
those asserts with always-on `CHECK` assertions, preserving execution and
test validity in Release instead of weakening compiler warnings.
**The fixed commit has not yet received a green full CI verification.**

The agent handoff contains exact failing job IDs, correction commits and the
final build/test matrix. InputFlow remains a Draft PR pending successful
CTest, editor/FPS/RTS native builds, multi-device input smoke and integration
with the newest inspector branch. A queued or pending CI job is never
evidence of a passing test.

## C++ basics

\`\`\`cpp
#include "engine/platform/input/InputFlowCore.h"
namespace ei = aiverse::lib::gameplay::inputflow;
ei::InputFlow input;
input.defineAction({"Move",ei::ValueType::Axis2D,{ei::Modifier::normalize()}});
input.defineAction({"Fire",ei::ValueType::Boolean});
ei::MappingContext game{"Gameplay"};
game.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::W)},
          {ei::Modifier::swapXY()});
game.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::S)},
          {ei::Modifier::swapXY(),ei::Modifier::negate()});
game.bind("Fire",{ei::Device::MouseButton,0},{},{ei::Trigger::pressed()});
input.registerContext(std::move(game));
input.activateContext("Gameplay",0);
aiverse::platform::InputFlowAdapter adapter(input);
// In the platform event callback:
adapter.onEvent(platformEvent);
// Once per presentation frame (if the app owns gamepad polling):
adapter.pollGamepads();
for(const auto& e:input.update(float(dt))) {
    if(e.action=="Fire" && e.phase==ei::Phase::Triggered) { /* queue a command */ }
}
const auto* move=input.state("Move");
if(move && move->active) { /* quantize move->value.x/y to a Command */ }
\`\`\`

\`Control\` is \`{Device,code,deviceIndex=0}\`. Gamepad index 0..3. The mouse axis is relative motion (X/Y), mouse wheel (X/Y) is a one-frame pulse. Axis values can exceed 1 for mouse deltas; add \`normalize\` intentionally to a movement action. Supported action types: Boolean, Axis1D, Axis2D, Axis3D. Mapping and Action modifiers: Scale, Negate, DeadZone (rescaled), Swizzle, Normalize. Triggers: Down, Pressed, Released, Hold(seconds), Tap(seconds), Chord(action name).

A single update may deliver multiple lifecycle events for one action (Started + Triggered; later Triggered + Completed). A tap entirely between updates can still produce a one-frame Triggered pulse. Hold is one-shot per press and needs \`update(dt)\` to advance. Multiple triggers on a mapping are AND-gated. Contexts are ordered by descending numeric priority; equal priority context names are ordered lexicographically, with mapping declaration order inside a context. Active higher-priority bindings consume controls by default; use \`consume=false\` for intentional sharing. \`deactivateContext\`, \`cancelAll\`, pointer lock changes and lost focus must not leave held input.

## Project-defined action assets

The optional FPS asset lives at \`<project>/input/actions.json\`. If it is absent the FPS reference client uses its built-in default FPS mapping. If present and valid it **replaces** those defaults completely. Do not ship an incomplete asset unless you intentionally omit those actions.

Example:
\`\`\`json
{
  "version": 1,
  "actions": [
    { "name": "Move", "type": "axis2D", "modifiers": [{ "type": "normalize" }] },
    { "name": "Aim", "type": "bool" },
    { "name": "Fire", "type": "bool" }
  ],
  "contexts": [
    {
      "name": "Gameplay", "priority": 0, "active": true,
      "mappings": [
        { "id": 1, "action": "Move", "control": { "device": "keyboard", "code": "W" },
          "modifiers": [{ "type": "swizzle", "order": [1, 0, 2] }] },
        { "id": 2, "action": "Move", "control": { "device": "keyboard", "code": "S" },
          "modifiers": [{ "type": "swizzle", "order": [1, 0, 2] }, { "type": "negate" }] },
        { "id": 3, "action": "Aim", "control": { "device": "mouseButton", "code": "right" } },
        { "id": 4, "action": "Fire", "control": { "device": "mouseButton", "code": "left" },
          "triggers": [{ "type": "pressed" }, { "type": "chord", "action": "Aim" }] }
      ]
    }
  ]
}
\`\`\`

\`device\` values: \`keyboard\`, \`mouseButton\`, \`mouseAxis\`, \`mouseWheel\`, \`gamepadButton\`, \`gamepadAxis\`, \`touch\`. Keyboard codes follow \`platform::Key\` names (\`W\`, \`LeftShift\`, \`Num1\`, etc.); mouse buttons: \`left\`, \`right\`, \`middle\`, \`x1\`, \`x2\`; mouse axis and wheel: \`x\`/\`y\`; pad buttons: \`A\`, \`B\`, \`X\`, \`Y\`, \`LB\`, \`RB\`, etc.; pad axes: \`LX\`, \`LY\`, \`RX\`, \`RY\`, \`LT\`, \`RT\`. \`deviceIndex\` defaults to 0, max 3 for pads. Numeric codes are accepted but string codes are preferred for editor usability.

Action \`type\`: \`bool\`, \`axis1D\`, \`axis2D\`, \`axis3D\`. Modifiers: \`scale\` with \`x/y/z\` numbers, \`negate\`, \`deadzone\` with \`threshold\` (0..0.999), \`swizzle\` with \`order\` (3 source indices), \`normalize\`. Triggers: \`down\`, \`pressed\`, \`released\`, \`hold\` or \`tap\` with \`seconds\`, \`chord\` with \`action\`. Each mapping may have \`consume\` (default true). Mapping \`id\` should be a stable positive integer for rebinding profiles. Strict schema rejects unknown fields, duplicate names/ids, missing actions, unsupported codes, invalid ranges and unresolved chord actions. Asset loading is transactional: a bad asset must leave the current \`InputSystem\` intact.

## Rebinding profiles

The user profile is independent of the project's authored asset and **only** stores mapping-id overrides:

\`\`\`json
{
  "version": 1,
  "rebindings": [
    { "context": "Gameplay", "id": 1,
      "control": { "device": "keyboard", "code": "Up" } }
  ]
}
\`\`\`

Use \`platform::enhanced_config::readProfile\`, \`applyProfile\`, \`writeProfile\` and \`InputSystem::rebind(context,id,Control)\`. A profile validates all context/ID targets before applying anything. The FPS sample reads/writes local profiles in \`platform::userDataDir()\` under \`enhanced-input-<stable-project-hash>.json\`; this never mutates project assets. \`App::rebindInput\` is the reference persistence hook for a future input-settings UI. When manually saving user settings on Web, call \`platform::syncUserData()\`.

## Reusing in other genres

- **RTS:** \`Select\`, \`AttackMove\`, \`CameraPan\` actions become local selection or validated RTS Commands. Separate \`UI\`, \`Modal\`, \`Gameplay\`, \`BuildingPlacement\` mapping contexts. Do not leak UI clicks into the world.
- **FPS:** fixed-tick \`fps::Input\` still handles sequence/viewTick/weapon actions; input device read and modifiers stay presentation-side.
- **Vehicles:** \`Steer\`, \`Throttle\`, \`Brake\`, \`Interact\` can combine digital keys, pedals and pad axes using actions, without a genre-specific platform backend.
- **Editor:** viewport, gizmo, text fields, modal dialogs and play-test should each own a mapping context. Respect ImGui/RmlUi capture before gameplay and preserve text composition.

## Validation and outstanding work

Run:
\`\`\`bash
cmake -S tests/libraries -B /tmp/aiverse-input-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aiverse-input-tests --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests
ctest --test-dir /tmp/aiverse-input-tests --output-on-failure -R 'library_enhanced_input'
python3 tools/libraries/catalog.py check
python3 tests/libraries/native_consumer.py
\`\`\`

Plus FPS build and sample matches on all supported platforms, plus engine full tests per \`AGENTS.md\`. Do not mark any platform verified without a successful build and tests.

Remaining before release: complete RTS camera/selection (legacy paths intentionally preserved), high-level pinch/two-finger gesture recognizers, multiplayer split-screen routing, advanced chord/conflict policies, scripted-game rebinding UI, and verified cross-platform CI / manual editor focus/pointer-lock checks. Basic editor action authoring, FPS player rebind, RTS semantic command actions, hosted focus edges and touch delta axes are implemented. These should be tracked in \`docs/enhanced-input-agent-handoff.md\` after each stage.

## Authoring mappings in AIVERSE Editor

Open **Logic → Input Actions** (window ID `input`) for any native project. The dockable editor window reads and writes `<project>/input/actions.json`. It shows editable Actions (Bool/Axis1D/2D/3D, Action modifiers and triggers), Mapping Contexts (enabled/priority), and their bindings (Action, device, code, device index, consume, mapping modifiers/triggers). Keyboard bindings support click-to-capture; Escape cancels without modifying the binding. A conflict warning appears for duplicate physical controls within a context. The window is registered automatically through `AIVERSE_EDITOR_WINDOW`.

**Save** validates the entire asset using the strict `readAsset` parser before writing. It writes a temporary JSON file and creates `.bak` before replacing an existing asset; a failed validation leaves the original untouched. The FPS sample regards this as an entire mapping override — creating an incomplete FPS asset can disable unmapped actions. Use **Reload** to abandon unsaved edits. Project-owned mappings and per-user rebinding profiles are intentionally separate. The current window edits authored project assets, **not** a running player's saved profile.

Capture temporarily suppresses global editor shortcuts (e.g. F5) and cancels when the window is no longer the selected dock tab. The editor's own viewport/tool shortcuts and the hosted game's legacy input routing are not yet fully converted; do not claim complete input capture arbitration until the RTS/hosted client stage is validated.

## RTS client integration

The generic RTS client uses `apps/client/ClientInputRouting.h` for `AttackMove`, `Patrol`, `Repair`, `Reclaim`, `Stop`, `Hold` and `FocusCamera`. Each action resolves from `RTS.Gameplay` Mapping Context; optional project `input/actions.json` overrides the complete default shortcut set, as in FPS. RmlUi/Modal capture runs first and semantic shortcuts cannot pass through captured UI. Scripted command hotkeys retain first refusal, followed by semantic Actions -> existing `issue(Command)` calls. Numeric control groups and client pointer gestures remain backward-compatible.

## In-game input rebinding (FPS example)

Press **F10** while playing the FPS sample to open Input Settings and release pointer lock. Select **Rebind** beside any mapping and press a new keyboard key, mouse button or gamepad button; moving a pad axis past 0.8 can bind that axis. The UI displays duplicate-control warnings within each context. Changes use the existing `rebindInput` and `platform::syncUserData` profile persistence; they do **not** change the project's authored `input/actions.json`. Escape cancels active capture. Close Settings and click the viewport to resume locked-pointer gameplay. Editor-side binding authoring remains in the Logic → Input Actions window.

## Complete default mapping presets

The shared engine header `engine/platform/EnhancedInputPresets.h` provides `enhanced_presets::fpsReference()` and `rtsReference()`, returning editable full assets with stable mapping IDs. Both reference clients use these presets, and **Logic → Input Actions → Use FPS defaults / Use RTS defaults** fills the editor buffer with the corresponding complete scheme. Save to write `<project>/input/actions.json`. Existing user rebinding profiles continue to apply by mapping ID. Avoid removing/reordering mapping IDs in a published project without planning a profile migration.

## Mobile touch controls

`EnhancedInputAdapter` supports up to 32 simultaneously tracked touches, each with a stable/recycled `deviceIndex` (0..31). For `Device::Touch`, numeric control code `0` is finger pressed (Boolean level); `1` is relative horizontal motion in logical points, `2` is relative vertical motion. The motion channels accumulate between events and clear after `InputSystem::update(dt)`, just like mouse deltas. Add `Modifier::scale` or `normalize` as appropriate for your touch gesture. Repeated TouchBegin and excess fingers are ignored. TouchEnd/TouchCancel release finger-down; FocusLost cancels all active touches. This is a low-level gesture foundation; pinch, two-finger swipes and gesture recognizers are not yet exposed as named high-level actions.

## Editor-hosted Game focus

Game window forwards keyboard input only while focused. Switching from Game to Inspector, Input Actions or another tab forwards synthetic `FocusLost` to the hosted client and cancels Enhanced Input and legacy RTS held keys/pointer capture. Returning to Game forwards `FocusGained`; the embedded client still preserves HostedView and ImGui context isolation.

## User-profile file integrity

`engine/platform/EnhancedInputProfileIO.h` implements validated `loadProfileFile` and `saveProfileFile`. A profile must pass the JSON schema before disk replacement; a temporary file is written first, with backup+restore handling where atomic rename-overwrite is unavailable. The FPS Settings UI applies a rebinding to a staged `InputSystem` copy and commits live input only **after** successful persistence. A write error leaves the old binding and profile active.

## Two-finger gesture inputs

The platform adapter also exposes gesture channels under `Device::Touch`, `deviceIndex: 0`: numeric `code: 3` is the change in distance between two fingers (pinch, logical pixels), `code: 4` is the horizontal change in their midpoint, and `code: 5` is the vertical change. They are relative deltas, accumulated per frame and cleared on `InputSystem::update(dt)`. No pulse is generated when contacts enter or leave the two-finger gesture. Bind these to semantic Actions such as `Zoom` and `CameraPan`, with project-specific scales; never process raw float controls inside ECS.

### RTS camera Actions

Default RTS mapping includes `RTS.Camera` context with `CameraPan` (arrow keys) and `IdleCameraPan` (WASD when there is no unit selection). Both actions are Axis2D and do not consume the underlying key, so other RTS hotkeys continue to work. Arrow and WASD input are combined with edge scrolling and legacy gamepad camera pan in the presentation layer, and never written directly to ECS. Old `RTS.Gameplay` mapping IDs remain stable.

### Editor GameWindow camera bindings

In the editor's direct RTS play-test, the Game window uses the same semantic `CameraPan` and `IdleCameraPan` mappings through its own presentation `InputSystem`. It samples bound keyboard keys only while the Game window owns focus and no text field is active. It reads optional project `input/actions.json` and refreshes the mapping shortly after it changes. Unlike the standalone RTS client, the editor retains historical always-available WASD camera navigation even while a unit is selected. In hosted-client mode, the embedded game uses its own action routing and the synthetic focus-loss bridge.

### Project asset file safety

`enhanced_config::loadAssetFile` / `saveAssetFile` in `engine/platform/EnhancedInputFileIO.h` validate the entire version-1 action/context schema before committing project-authored `input/actions.json`. Project files and per-user profiles now share the same temporary-file and backup/restore writer. The editor no longer deletes its existing project asset before an unverified rename. Invalid actions and controls leave existing project data unchanged.

### RTS pointer Actions and camera zoom

Default `RTS.Pointer` context provides `Select` (left click), `Command` (right click) and `CameraZoom` (mouse wheel Y). The generic RTS client now evaluates these through the normalized input adapter after RmlUi pointer ownership has been decided; its zoom uses the semantic Axis1D value. Drag selection, smart orders, touch ownership, garrison gestures and authored scripts remain their existing high-level handlers. The pointer context has non-consuming bindings so shared RTS mechanics can coexist with the new Actions.

### Hidden Game tabs cannot capture input

When the embedded Game tab is hidden behind another dock tab, `Editor::event` now stops forwarding ordinary input into its client and calls `ClientHost::suspendInput()`. This sends a hosted `FocusLost` and clears any captured keyboard/pointer/touch interaction, including during a rebind operation in Input Actions. Global OS focus loss is still forwarded. Restoring the Game tab re-establishes focus through the existing `ClientHost::place` lifecycle.

### Modifier-to-trigger ordering

Action-level modifiers run before the final action value determines lifecycle. For example, an Axis1D action with an Action `DeadZone(0.3)` is inactive for stick values below the threshold and cannot emit a normal `Triggered`; moving past the threshold starts it, dropping below completes it. Mapping-level Pressed/Tap/Released pulses retain their momentary event behavior.

## Input-specific CI

`.github/workflows/enhanced-input.yml` checks standalone input core, platform adapter, JSON/profile persistence and RTS routing on Linux, macOS and Windows. It is a focused cross-platform gate, **not** a substitute for full `aiverse_editor`, `aiverse_client`, `aiverse_fps_client`, mobile/web builds or interactive editor validation. Review the workflow run itself before claiming these tests have passed.

## Conflict-aware rebinding

`InputSystem::rebind(context,id,control,policy)` accepts `RebindPolicy::Allow` (default), `Reject` (same-context duplicate forbidden) and `Swap` (exchange with exactly one colliding mapping). Existing mapping IDs stay stable. `InputSystem::conflicts(activeOnly=true)` returns ordered conflicts describing the shadowed Action when the earlier mapping consumes the key; duplicate controls may still be intentional (e.g., different triggers and modal contexts).

### Advanced triggers

`DoubleTap(window)` triggers on a second press within the allowed period after release. `HoldAndRelease(seconds)` fires after holding long enough and then releasing. `Pulse(interval)` repeats periodically while held. `NotChord(action)` blocks its mapping while that named action is actuated. JSON trigger names: `doubleTap`, `holdAndRelease`, `pulse`, `notChord`; the corresponding editor trigger fields are editable. These are all presentation events; projects convert them to existing deterministic Commands.

## Multiple local players

`engine/platform/EnhancedInputUsers.h` exposes `platform::EnhancedInputUsers`, a per-local-user ownership router. Register one `InputSystem` per user with `addUser(id,scheme)`; assign keyboard, pointer, touch independently or a physical pad with `assignGamepad(pad,id)`. Forward the captured platform event through `onEvent(e, gameplayAllowed)`, call `pollGamepads()` **once** per frame, then `update(id,dt)` for each player. Each user's gamepad mappings use logical index 0 regardless of the physical pad. Reassigning devices or losing focus cancels held Actions, so controls do not leak between split-screen players. The router is an opt-in library for multi-player native projects and does not change existing single-user projects.

## Top-down shared Action scheme

RTS sample includes an optional `RTS.TopDown` mapping context (disabled for ordinary RTS) with `TopDownMove` Axis2D, `TopDownFire`, `TopDownAim`, `TopDownEnter`, `TopDownWeaponNext`, `TopDownWeaponPrev`, `TopDownWeapon1` through `TopDownWeapon9`. The client activates it only while controlling a top-down hero. Movement/fire/aim and weapon/enter Actions are evaluated through Enhanced Input, with old key fallback when a custom project schema intentionally omits the TopDown context. Configured game-specific ability buttons and HUD touch sticks remain the specialized `TopDownControls` layer.

### Editor/FPS rebinding collision policies

The project Input Actions editor and FPS F10 Settings each expose Allow duplicate, Reject duplicate and Swap keys. Swap exchanges controls between exactly two colliding mappings while preserving their mapping IDs; a three-way collision is intentionally rejected. The FPS user profile persists **both swapped mapping overrides** transactionally, and the UI shows conflicts across mapping contexts, including whether an earlier binding consumes input and shadows a later Action. Trigger-based shared buttons remain valid when Allow is chosen.

## Project-specific per-user profiles

`engine/platform/EnhancedInputUserProfiles.h` exposes `UserProfiles` for any local user ID. It saves validated, atomic profile overrides per project and player, including both mappings changed by Swap. `load` applies them transactionally, `rebind` persists and commits atomically, and `reset(userId,system,projectDefaults)` restores the project binding scheme without editing it. Player 0 uses the original FPS profile filename for compatibility; other local users receive separate `-user-<id>` files. FPS F10 Settings now uses this shared service and includes Restore project defaults.

### RTS camera gesture Actions

Default `RTS.Gestures` context maps touch slot-zero relative drag to `CameraTouchDrag` Axis2D, two-finger centroid motion to `CameraTouchPan` Axis2D and two-finger pinch to `CameraPinch` Axis1D. The RTS client now uses these semantic values for camera pan/zoom while preserving terrain-ground projection and existing touch-selection behavior. Scale the values through authored Action modifiers rather than rebuilding the device gesture algorithm. Projects with older complete custom input assets retain the previous raw-delta fallback.

### RTS Select/Command mouse rebinding

`RTS.Pointer` controls now affect the actual world gesture entrypoints, not merely Action event notifications. The native event is delivered unchanged to the UI first; after UI capture, `ClientInputRouting::logicalMouseButton` translates an authored mouse button to logical Select (0), Command (1) or camera pan (2) for the world handler. Unbound buttons do not select/order. The pointer's position, drag-box geometry, garrison logic and existing validated Commands remain unchanged.

### Generated top-down ability mappings

Top-down's game-specific button definitions in `game.json.topDown.buttons` are registered at runtime as `TopDownAbility0`, `TopDownAbility1`, etc., in the optional `RTS.TopDownAbilities` context. Their press/release is routed through semantic Actions; UI touch-stick buttons and the actual ability Commands remain the responsibility of the top-down module. A project can author a custom context with that name in `input/actions.json` to override generated keys without altering engine code.

### Action updates and timers

Game clients must call the input pipeline every presentation frame, even when no keyboard/mouse events arrive. RTS now combines immediate `onEvent(e)` evaluation with `advance(dt)` for Hold/Pulse and other timed triggers, routing both through the same validated gameplay Command handler. Top-down custom ability Actions also handle delayed triggers and release on context cancellation. Modal/text capture cancels held actions, while script-author-defined hotkeys still take precedence over generic RTS bindings.

### Editor direct-play pointer bindings

The editor Game window's direct RTS play-test now uses `RTS.Pointer` for mouse Select/Command, just like the standalone game client. This affects box selection, scripted map clicks and building placement while retaining editor viewport coordinates. Middle-mouse camera panning yields if that button is explicitly bound to Select or Command. Project bindings are reloaded before script-card hit testing.

### Two-finger twist

Touch gesture control code 6 (deviceIndex 0) is a signed relative rotation in radians, wrapped to ±π and reset on finger-count changes or focus loss. The default RTS input scheme maps it to `CameraTwist` Axis1D, allowing per-project sensitivity modifiers for planetary/camera rotation. It complements codes 3/4/5 for pinch/centroid pan and stays separate from world-space camera math.

## Planet and possessed-unit Actions

The default RTS preset now includes two additional optional contexts: `RTS.Planet` with `PlanetRotate`, `PlanetPitch`, `PlanetNextBody`, and `RTS.Possession` with `PossessedMove`, `PossessedFire`, `PossessedWalk`. The engine activates each only in its corresponding client mode. Planet view uses common camera pan/zoom/touch gesture Actions—including pinch, two-finger pan and twist—while keeping orbital projection/planet selection client-owned. Possession uses an Axis2D camera-relative direction and mouse-held fire/walk Actions, still issuing the original deterministic simulation Commands. Old custom full input assets lacking these actions retain their original control fallbacks.

### Chord condition evaluation

`Chord` and `NotChord` constraints are evaluated inside the same iteration over the mapping's trigger list. This preserves combined positive/inverted chord semantics and fixes a C++ variable-scope error found during the implementation audit. Automated compilation has not yet been run.

### Chord priority and deadzones

Chord/NotChord presence checks use a preview of active mapping values in normal context-priority order. A control consumed by an earlier active context cannot also act as a lower-priority chord modifier; mapping/Action modifiers and typed dimensions are applied before determining whether the referenced Action is actuated. This preview does not independently evaluate recursive chained trigger dependencies.


## Canonical InputFlow native API

New game code uses the branded API; old EnhancedInput* includes remain available.

```cpp
#include "engine/platform/InputFlow.h"

namespace flow = aiverse::lib::gameplay::inputflow;

int main() {
flow::InputFlow input;
input.defineAction({"Jump", flow::ValueType::Boolean});
flow::InputContext gameplay{"Gameplay"};
gameplay.bind("Jump",
    {flow::Device::Keyboard, int(aiverse::platform::Key::Space)},
    {}, {flow::Trigger::pressed()});
input.registerContext(gameplay);
input.activateContext("Gameplay");
aiverse::platform::InputFlowAdapter adapter{input};
// Event callback: adapter.onEvent(e); frame: input.update(dt);
// Route semantic ActionEvent into validated project Commands.
(void)adapter;
return 0;
}
```

The public platform facade `engine/platform/InputFlow.h` exports
`InputFlowAdapter`, `InputFlowUsers`, `InputFlowUserProfiles`,
`InputFlowAsset`, `InputFlowProfile`, `inputflow_config` and
`inputflow_presets`. FPS/RTS/editor now use the canonical namespace.
The FPS UI is **InputFlow Settings (F10)**; the editor window is
**InputFlow Actions**. The target `AIVERSE::lib_input`, file
`input/actions.json`, legacy `enhanced::InputSystem` and existing
profile filenames are preserved for compatibility.

CI is displayed as **AIVerse InputFlow targeted checks**, but the
existing test target names remain unchanged for test-runner compatibility.

**Canonical implementation types:** `aiverse::lib::gameplay::inputflow::InputFlow`, `aiverse::platform::InputFlowAdapter`, and `aiverse::platform::InputFlowUsers` are now the actual C++ class names. The older `enhanced::InputSystem`, `platform::EnhancedInputAdapter` and `platform::EnhancedInputUsers` remain type aliases for source compatibility. Headers and other host integration paths are not renamed to avoid disrupting existing projects.
