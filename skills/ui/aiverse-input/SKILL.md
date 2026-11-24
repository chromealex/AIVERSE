---
name: aiverse-input
description: Build games with AIVerse InputFlow, the genre-independent C++ Actions, Mapping Contexts, Modifiers, Triggers and rebinding system. Preserve legacy input helpers.
---

# AIVerse InputFlow (presentation only)

Read `docs/enhanced-input.md` and `docs/enhanced-input-agent-handoff.md` **before changing the input system**. Read `AGENTS.md` and `libraries/catalog.json` in engine root. The handoff contains stage status, all changed files, test coverage, caveats and next-agent instructions. The documentation MUST be updated **after each stage** — no stage is finished without its handoff section.

**InputFlow chord policy (Stage 47):** `Chord` and `NotChord` resolve through
the *effective* same-frame Action graph, including transitive Action- and
Mapping-level gates and Action modifiers. JSON and native definitions reject
cyclic dependencies (also inverted cycles) transactionally. When sharing a
physical control, use `consume=false` where simultaneous delivery is intended.
Stage 48 adds a bounded gate-aware refinement so a false high-priority mapping
Chord does not steal a lower-priority binding. Feedback patterns involving the
same consumed prerequisite control still use a deterministic conservative
fallback. See `docs/enhanced-input.md` and the Stage 47–48 handoff. Tests are
written but not yet executed; do not mark InputFlow validated or production-ready.

**Stage 49 lifecycle:** when a Mapping/Action chord becomes false or a
higher Context consumes its control, the active Action cancels and Hold/Pulse
timers restart only after eligibility returns. Reactivation of a context
initializes Mapping press history from current held controls. Do not treat
physical down as sufficient for `ActionState.active`; inspect `state()`
after the Action-level gates. Tests authored, not run. See handoff Stage 49.

**Stage 50 multi-user:** use `InputFlowUsers` for logical per-player
device owners and poll the physical gamepad backend once per frame. A keyboard,
pointer, touch or gamepad ownership transfer now cancels **only** the affected
device family (`InputFlow::cancelDevices`), leaving the user's other held
devices active. Consume resulting `Canceled` events on the next `update`.
For global window FocusLost use `cancelAll`, not selective cancellation.
New `clearKeyboardOwner/clearPointerOwner/clearTouchOwner` unassign ownership.
Check `docs/enhanced-input.md` and Stage 50 handoff. Tests not executed.

**Stage 51 no phantom edges:** activating/replacing a Mapping Context
over an already-held persistent control suppresses Mapping and Action
Pressed/Released/Tap/DoubleTap/Hold/Pulse trigger edges until physical release,
while continuous Down-level Actions remain visible. Avoid replaying stale OS
KeyDown on focus regain. Tests authored but NOT RUN; see Stage 51 handoff.

**Stage 52 RTS captured input:** `ClientInputRouting::onEvent(e,false)`
suspends only the event's device class, so UI mouse capture cannot erase
active keyboard camera movement. The RTS client also blocks legacy
`keys_` fallback on RmlUi/text/modal capture and clears it when gameplay
keyboard input is disabled. Real UI/native regressions NOT RUN.

**Stage 53 editor direct GameWindow:** editor direct play samples keyboard,
mouse buttons/axes and wheel through the same InputFlow runtime before
script-card pointer processing. Use `InputFlow::actionDefinition(name)`
to inspect a read-only Action's trigger rules; semantic `Triggered` handles
Hold/Tap/DoubleTap/etc. instead of unconditionally using raw
`ImGui::IsMouseClicked`. Do not sample twice in hosted ClientHost mode.
ImGui/native smoke NOT RUN.

**Stage 54 conditional priority:** both Mapping- and Action-level
Chord/NotChord gates affect whether a high-priority mapping consumes its
physical key. A disabled high Action must allow a lower context to use the
key. Dependency refinement is bounded by the number of Actions (cap 512),
with deterministic conservative fallback for contradictory feedback.
Cycles are rejected in native/JSON authoring; direct Action graph chains
may exceed eight levels. Tests AUTHORED, NOT RUN.

**Validation Stage 56 (not release-ready):** all targeted InputFlow CI
jobs were still queued when last inspected. The full Linux ARM64 CI failed
in unrelated ECS regression tests because standard `assert` vanished in
Release `-DNDEBUG`; both test files have since been changed to always-on
`CHECK`, but green retests have not been observed. Do not mark PR #20
ready or merge it until native tests, editor/FPS/RTS integration and
multi-device smoke are actually confirmed. Review Stage 56 in the handoff
for job IDs and corrected commits.

## Canonical InputFlow API

For native CMake projects link `AIVERSE::InputFlow` (or `AIVERSE::lib_inputflow`), both aliases of the original header-only `aiverse_lib_input` target. `AIVERSE::lib_input` is retained for older projects.

For new C++ games include `"engine/platform/input/InputFlowCore.h"` and use `aiverse::lib::gameplay::inputflow::InputFlow` with `InputAction`, `InputContext`, `InputValue` and the other re-exported types. For platform hosts include `engine/platform/InputFlow.h`, using `platform::InputFlowAdapter`, `platform::InputFlowUsers`, `platform::InputFlowUserProfiles`, `platform::inputflow_config` and `platform::inputflow_presets`. Keep `EnhancedInput.h`, `enhanced::InputSystem`, `AIVERSE::lib_input`, `input/actions.json` and existing user profiles working for older games. Do not rename an authored asset or a serialized gameplay command to achieve branding.

## Pick the existing mechanism first

- Legacy `engine/gameplay/Input.h`: single-context `resolveInput`, integer `deadzone`, `ControlGroups`, `updateSelection`, `DragGesture`. Never remove or silently break it.
- Canonical `engine/platform/input/InputFlowCore.h`: `aiverse::lib::gameplay::inputflow::InputFlow`, typed Actions and Values, Mapping Contexts, Modifiers, Triggers, lifecycle events and rebind policies. `EnhancedInput.h` and `enhanced::InputSystem` are compatibility aliases.
- `engine/platform/EnhancedInputAdapter.h`: bridge from `platform::Event` and `GamepadState` into generic Controls. Focus loss cancels pressed state. Host must not poll gamepads twice per frame.
- `engine/platform/EnhancedInputConfig.h`: validated `Json` project mapping asset `readAsset/installAsset/writeAsset` and per-user `readProfile/applyProfile/writeProfile`. Includes engine `Json` (not standalone lib-only code).
- FPS reference: `apps/fps/client.cpp` maps Action state/events to existing `fps::Input` network commands, without changing authoritative protocol.
- For settings read `docs/gameplay-settings.md`; do not invent keys in `libraries/spec/gameplay.json`.

## Implementation workflow

1. Identify if the feature is already an Input Action or an ECS gameplay mechanic. Use an action/context first; NEVER duplicate key handling per genre or write keyboard/mouse logic into ECS systems.
2. Define semantic actions (`Fire`, `Move`, `PanCamera`, `Interact`) with Boolean / Axis1D / Axis2D / Axis3D. Define physical mappings and optional modifier/trigger chains in a named context, not hardcoded inside a simulation tick. Compose UI, modal, gameplay and vehicle contexts by priority; UI/modal consume input before gameplay.
3. Use `platform::EnhancedInputAdapter::onEvent` for key/mouse/wheel/touch, `pollGamepads` once per presentation frame, and `InputSystem::update(dt)` to evaluate actions. Consume both `ActionState` (held level) and lifecycle events (`Started`, `Ongoing`, `Triggered`, `Completed`, `Canceled`). Preserve very short taps/one-shot actions by queuing them until the next command tick if needed.
4. Author an optional `<project>/input/actions.json` with stable mapping IDs; load via `platform::enhanced_config::readAsset` and transactionally `installAsset`. User-specific key overrides go to a profile (NOT `rules.json`), e.g. `platform::userDataDir()`. `applyProfile` validates every mapping target before applying; protect users from invalid / future profiles.
5. Convert actions into validated **Commands** before entering the simulation. The sim and ECS operate on integer/Fixed inputs and replicate Commands; raw floats and device events stay in presentation. Local camera and selection can remain local state; authoritative actor motion must not depend on client wall time.
6. For C++ native modules, `aiverse_native_use_libraries(aiverse_native_ID input)` / `AIVERSE::lib_input`. The pure library is usable without Lua or engine code. Include `"engine/platform/input/EnhancedInputCore.h"`. Platform/JSON helpers additionally need engine headers from host, not from the pure library target.
7. Preserve old game controls and network formats during migration; migrate clients incrementally. Test focus loss, pointer lock, UI capture, two contexts using the same key, simultaneous chords, invalid rebinds, gamepad disconnect, deadzones and mouse deltas.
8. **Immediately after each stage**, append the changed files/APIs, verification commands and exact results, known unverified platforms, open issues and a precise next-agent checklist to `docs/enhanced-input-agent-handoff.md`. Keep earlier stages. Update this skill/catalog/docs together when the library API changes.

## API sketch

```cpp
namespace ei = aiverse::lib::gameplay::enhanced;
ei::InputSystem input;
input.defineAction({"Move",ei::ValueType::Axis2D,{ei::Modifier::normalize()}});
input.defineAction({"Fire",ei::ValueType::Boolean});
ei::MappingContext gameplay{"Gameplay"};
gameplay.bind("Move",{ei::Device::Keyboard,int(aiverse::platform::Key::W)},
              {ei::Modifier::swapXY()});
gameplay.bind("Fire",{ei::Device::MouseButton,0},{},{ei::Trigger::pressed()});
input.registerContext(std::move(gameplay));
input.activateContext("Gameplay",0);
input.setControl({ei::Device::MouseButton,0},1);
for(const auto& event:input.update(1.f/60)) { /* queue commands on Triggered */ }
```

Use `Modifier::scale/negate/deadZone/swapXY/normalize`, `Trigger::down/pressed/released/hold/tap/chord`, `InputSystem::rebind(context,mappingId,control)`. For mouse motion use `addDelta`; its values are intentionally NOT clamped to [-1,1]. A "hold" trigger fires once per press, while a "down" trigger fires while active; multiple trigger requirements on one mapping are AND-gated.

## Mandatory validation

```sh
cmake -S tests/libraries -B /tmp/aiverse-input-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aiverse-input-tests --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests
ctest --test-dir /tmp/aiverse-input-tests --output-on-failure -R 'library_enhanced_(input|rts_input)'
python3 tools/libraries/catalog.py check
python3 tests/libraries/run.py
python3 tests/libraries/native_consumer.py
```

For FPS/client integration also build `aiverse_fps_client`, run sample tests and full engine verification per `AGENTS.md`. Run GCC and Clang/MSVC/Apple CI where possible; do not claim a platform has been verified without success. Review `docs/enhanced-input-agent-handoff.md` for current stage and outstanding work.


In addition to the commands above, verify the workflow `.github/workflows/enhanced-input.yml` (Linux/macOS/Windows). Its green status is necessary but not sufficient: editor/FPS/client full builds and a manual hosted-focus/rebinding smoke test are mandatory before merging.


For local multiplayer use `engine/platform/EnhancedInputUsers.h` (`EnhancedInputUsers`) rather than independently calling `EnhancedInputAdapter::pollGamepads()` for each local user. Assign physical devices to unique user IDs, use gamepad deviceIndex 0 inside each user's scheme and call `pollGamepads` once per frame. Route Action outputs to each player's existing validated Commands, never raw controls into simulation.


For per-player rebinding use `engine/platform/EnhancedInputUserProfiles.h` (`enhanced_config::UserProfiles`). It supports per-project/per-local-user load/rebind/reset and preserves the original player-zero profile path. Do not write ad hoc profile files or persist only one side of a Swap. Restore project defaults by saving an empty profile and replacing the user's runtime mapping with the unchanged project scheme.

**Canonical class names in implementation:** `inputflow::InputFlow`, `platform::InputFlowAdapter` and `platform::InputFlowUsers`. The historical `enhanced::InputSystem`, `platform::EnhancedInputAdapter` and `platform::EnhancedInputUsers` remain aliases only. Use the new names in generated code; keep legacy source-compatible names available for existing projects.
