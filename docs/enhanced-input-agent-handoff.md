# AIVerse InputFlow — agent handoff (living document)

Branch: `feature/aiv990_enhanced_input` (base `feature/universal-entity-inspector-fps`). Owner: input subsystem. Updated 2026-11-18.

**Mandatory workflow:** after EVERY stage update this file **in the same branch, before moving to another stage**. Record (1) numbered stage/date/goal/status, (2) architecture/API/invariants and exact file paths/commit, (3) the build/test commands and actual PASS/FAIL/NOT RUN evidence, (4) blockers, limitations and risks, and (5) a precise next-agent task with file/function entrypoints and acceptance criteria. No stage is considered complete without its handoff update. Never overwrite older stages or infer successful tests from queued CI. Keep `docs/enhanced-input.md` and `skills/ui/aiverse-input/SKILL.md` synchronized with any public changes. The user currently wants implementation first, testing later. See the binding rule in root `AGENTS.md`.

**Continuation protocol:** read this document's most recent stages, inspect the current Git branch head before writes, make a stage-scoped change, append the stage handoff and commit it, then begin the next stage. The next agent must be able to continue without asking the user to repeat architectural decisions.

## Stage 1 — audit and design (complete)

### Existing code
- `engine/gameplay/Input.h`: `InputPhase`, `InputEvent`, `Binding`, `resolveInput` (single best priority match, stable ID tie-break), integer `deadzone`; also selection/control-groups/drag gestures. This is the old public API: **do not remove or break**.
- `engine/platform/Platform.h`: platform-normalized `Event` for KeyDown/Up, MouseDown/Up, MouseMove (delta), Wheel, Touch, FocusLost. `Key` enum and modifiers.
- `engine/platform/Gamepad.h`: `pollGamepads`, four standard-layout pads, `PadButton`, `PadAxis`, button edges, per-axis floats.
- `apps/fps/client.cpp`: directly maintains pressed `std::set<platform::Key>`, weapon/action one-shots, mouse look and fire; builds `fps::Input` commands per simulation tick.
- RTS in `apps/client` and editor input in `apps/editor` have their own event/UI handling. Avoid mass-rewriting all clients at once; isolate input evaluation from deterministic sim commands.
- `skills/ui/aiverse-input/SKILL.md` and `libraries/catalog.json` describe the current `input` package, currently v0.1.1.
- `tests/libraries/gameplay_tests.cpp` tests the legacy library API. Preserve these tests.

### Target architecture (Unreal Enhanced Input-inspired, **not** Unreal binary/API compatibility)
`Platform events/gamepads -> normalized device controls -> active prioritized Mapping Contexts -> mapping Modifiers -> Action modifiers/triggers -> Action event states -> game/editor/UI callbacks -> validated deterministic Commands -> ECS systems/simulation.`

Provide:
1. Named Input Actions with Bool / Axis1D / Axis2D / Axis3D values; consistent normalized vectors.
2. Mapping Contexts activated/deactivated at runtime with priorities. Higher active contexts may consume controls; UI and modal contexts override gameplay without changing bindings.
3. Mapping modifiers (scale, negate/invert, scalar deadzone, axis swizzle, normalize) and triggers (press, release, hold, tap, chord) with explicit state edges (Started, Ongoing, Triggered, Completed, Canceled).
4. Keyboard, mouse buttons, mouse move/wheel, gamepad axes/buttons; pointer lock, focus loss; no sim-specific representation here.
5. Rebindable profiles and serialization, without overriding engine `game.json` and without project-specific hardcodes.
6. Native C++ library access via existing `AIVERSE::lib_input` target; no Lua requirement.
7. Sufficient tests and example integration in FPS, then incremental RTS and editor migration.

### Invariants
- Presentation-side values may be float; **simulation never reads device state**, only validated/tick-stamped Commands (integer/Fixed). Do not duplicate input state into authoritative ECS. ECS systems consume commands produced from actions; local input resources may be presentation-only.
- Input lost focus, modal capture, or context removal must release held state, not leave buttons stuck.
- At equal priority, processing must be stable and documented. Chords must use action states, not ad hoc raw-key checks.
- Do not break `Input.h` legacy classes, gamepad conventions or existing serialized FPS/RTS command layouts.
- Per-project actions and contexts are data; the engine must not hardcode game-specific schemes.
- Tests include edge transitions, timing, chord precedence, deadzone/inversion, multi-device, context suppression, rebinding and focus loss.

### Stage plan
- **Stage 2:** implement the pure reusable Enhanced Input core, unit tests, native example; update this handoff.
- **Stage 3:** implement platform event/gamepad adapter and integrate representative FPS controls; update this handoff.
- **Stage 4:** profile JSON/asset loading and rebinding support, agent skill/catalog updates; update this handoff.
- **Stage 5:** RTS/editor context integration, cross-platform validation and remaining compatibility; update this handoff. (May require additional PR if editor migration would collide with active editor work.)

### Stage 1 verification
- Inspected repo over GitHub connector (main branch). No code or build changes in this stage.
- The container cannot reach GitHub directly (DNS unavailable); use the connected GitHub tools for reads/writes. For compilable standalone header tests, local compiler is available. Full project build on user's Mac or CI not yet performed.

### Next agent action
Implement Stage 2 in `libraries/include/aiverselib/gameplay/EnhancedInput.h`. Keep the legacy `Input.h` intact, add new focused tests under `tests/libraries` (or standalone no-engine test if appropriate), then document exact APIs and verification here before proceeding.

## Stage 2 — reusable action/context core + tests (implementation written; CI still pending)

**Changes on this branch**
- New \`libraries/include/aiverselib/gameplay/EnhancedInput.h\`: header-only C++20 \`aiverse::lib::gameplay::enhanced\` namespace. Legacy \`gameplay/Input.h\` is unchanged.
- New \`tests/libraries/enhanced_input_tests.cpp\`: movement vector composition/normalization, boolean key phases, hold/tap/release, same-frame tap, chord, priority override, rebind, focus cancel, analog deadzone, relative mouse delta.
- \`tests/libraries/CMakeLists.txt\`: added \`aiverse_enhanced_input_tests\` linked only to \`AIVERSE::lib_input\` and CTest \`library_enhanced_input\`; added a missing \`endif()\` for the existing \`voxel packages\` conditional at the end. Confirm this closure with CMake on next build.

**C++ API (Stage 2)**
\`\`\`cpp
#include <aiverselib/gameplay/EnhancedInput.h>
using namespace aiverse::lib::gameplay::enhanced;
InputSystem input;
input.defineAction({"Move", ValueType::Axis2D, {Modifier::normalize()}});
MappingContext gameplay{"Gameplay"};
gameplay.bind("Move", {Device::Keyboard, /* platform::Key::W */ 23}, {Modifier::swapXY()});
input.registerContext(std::move(gameplay));
input.activateContext("Gameplay", 0);       // higher priority wins; equal uses name order
input.setControl({Device::Keyboard, 23}, 1); // down
for (const ActionEvent& e : input.update(1.f/60)) {
  // e.action, e.phase (Started/Ongoing/Triggered/Completed/Canceled), e.value
}
input.deactivateContext("Gameplay");
input.cancelAll();                          // focus lost; sends Canceled and resets held controls
\`\`\`
Control codes above are **illustrative**; use \`int(platform::Key::W)\` in real platform glue rather than 23.

Available:
- \`ValueType\`: Boolean, Axis1D, Axis2D, Axis3D. \`Value{x,y,z}\`.
- \`Device\`: Keyboard, MouseButton, MouseAxis, MouseWheel, GamepadButton, GamepadAxis, Touch; \`Control{Device, code, deviceIndex=0}\`.
- \`Modifier\` factories: \`scale(x,y,z)\`, \`negate()\`, \`deadZone(t)\`, \`swapXY()\`, \`normalize()\`.
- \`Trigger\` factories: \`down()\`, \`pressed()\`, \`released()\`, \`hold(seconds)\`, \`tap(seconds)\`, \`chord(actionName)\`.
- \`MappingContext::bind(action, Control, mappingModifiers, mappingTriggers, consumes=true)\`; \`InputSystem::{defineAction,registerContext,activateContext,deactivateContext,clearContexts,rebind,setControl,addDelta,update,events,state,cancelAll}\`.
- Stable ordering: contexts high priority first; tie by context name; mappings in declaration order. Consumed controls cannot activate mappings in lower-priority contexts. Chords sample same-frame raw action levels. Relative deltas reset after update. Trigger timing uses supplied deltaSeconds (not wall-clock).
- An action may emit multiple events in a frame, e.g. Started+Triggered; Released/Tap can yield Triggered+Completed. \`ActionState.active\` is the held level; action state \`triggered\` latches a prior trigger during that hold. \`Trigger::hold\` is one-shot until release.
- \`Mapping.id\` is allocated when the context is registered; \`rebind(contextName, mappingId, newControl)\` changes one mapping.

**Verification performed**: source-level API audit and GitHub commits. Full compiler/CTest runs are **not yet performed**: GitHub remote is not reachable from the local build container; connected GitHub access is available. Run the new standalone target as the first validation step, then \`python3 tests/libraries/run.py\` + \`python3 tests/libraries/native_consumer.py\`.

**Known limitations / follow-up**
1. There is no platform bridge yet. Implement \`engine/platform/EnhancedInputAdapter.h\` next; do not mix UI text composition events with gameplay actions.
2. Mapping IDs must remain unique within contexts; future hardening should reject explicit duplicate IDs.
3. Profile JSON, persisted rebindings, runtime bindings editor and migration of existing FPS/RTS/editor are not yet included.
4. Chord prepass currently uses raw levels from active contexts (before consumption), so validate complex overlapping modal/chord scenarios.
5. Validate GCC/Clang/MSVC warning clean builds before calling Stage 2 verified.

### Next agent action
Stage 3: add a tiny adapter for \`platform::Event\` and \`platform::GamepadState\` with correct focus-loss handling and normalized mouse/gamepad mappings. Switch at least the FPS movement and fire input path to action states/events without changing \`fps::Input\` network command layout; add adapter smoke tests. Update this document immediately after that stage.

## Stage 3 — platform adapter + FPS reference migration (written; verification pending)

**Changed files**
- \`engine/platform/EnhancedInputAdapter.h\`: \`platform::EnhancedInputAdapter\`. \`onEvent(platform::Event)\` converts KeyDown/Up, MouseDown/Up, relative MouseMove axes, MouseWheel, TouchBegin/End/Cancel and FocusLost; \`pollGamepads()\` converts the 4 pads from \`platform::Gamepad.h\`; \`focusLost()\` cancels all held action states. Text/IME and UI events stay outside this layer. Polling same pad backend twice per platform frame is not supported: caller must own the poll.
- \`apps/fps/client.cpp\`: no \`std::set<Key>\` or per-key direct movement/fire logic. New \`App::configureInput()\` defines semantic FPS Actions/Context, including Move/Look/LookStick, Fire/Aim/Jump/Crouch/Sprint/Reload, inventory, weapon slots, reload, melee/block, throwable, interaction and buy. The project uses \`platform::EnhancedInputAdapter\` and reads \`ActionState\`/events before each simulation tick. One-shot Jump/Fire are queued until the next successful command submit. The authoritative \`fps::Input\` protocol, simulation and serialization are unchanged.
- Gamepad 0 is bound: left-stick move, right-stick camera look, triggers Fire/Aim, buttons A jump/B crouch/X reload/Y interact and LStick sprint.
- \`tests/libraries/enhanced_input_adapter_tests.cpp\`: keyboard, mouse button/motion/wheel, touch and mocked gamepad mapping; FocusLost cancel.
- \`tests/libraries/CMakeLists.txt\`: target/CTest \`aiverse_enhanced_input_adapter_tests\` / \`library_enhanced_input_adapter\` linked to \`AIVERSE::lib_input\` with \`GamepadCommon.cpp\`.
- \`EnhancedInput.h\`: Axis1D/2D/3D values remain unbounded (important: mouse delta in pixels cannot be clamped to [-1,1]); apply \`Modifier::normalize\` or DeadZone deliberately for normalized sticks/movement, not for mouse deltas.

**Behavioral contract**
- FPS gameplay actions are fed only while pointer-locked; clicking the main view first acquires pointer lock. Escape/focus loss cancels gameplay input immediately. This preserves the legacy gameplay capture model. A dedicated menu/input context can replace this in a later stage.
- \`InputSystem::update(dt)\` is presentation-frame-driven; action state is sampled by the fixed network/simulation command tick. Relative mouse axis applies once per frame. Gameplay state does not leak into deterministic ECS.
- Temporary FPS default mappings live in \`App::configureInput()\` until project-editable mapping assets are implemented in Stage 4. Do not duplicate these defaults for every project.
- The adapter exposes 4 gamepads. The FPS sample currently binds pad index 0 only, ready for project-specific player contexts.

**Verification:** Source inspection and tool commits only; no compiler run or CTest run yet. Review new FPS mapping code under Clang, particularly include/link dependencies and every transition on lost pointer lock. CI on a draft PR is the next validation path.

**Next agent**
Stage 4 must add validated action/context JSON schemas and per-user rebinding profile persistence, without changing \`rules.json\` gameplay data contracts. Update input skill and catalog, install tests for invalid JSON, save/load roundtrip and override priorities. Then update this handoff immediately. Stage 5: migrate RTS/editor to configurable contexts, expose actions UI, verify all sample projects.

## Stage 4 — validated project assets + user profiles + agent skill/catalog (written; CI pending)

**Code**
- \`engine/platform/EnhancedInputConfig.h\`: JSON \`Asset\` / \`ContextConfig\` / \`Profile\` / \`Rebind\` models and schema-version 1. \`readAsset\`, \`writeAsset\`, \`installAsset\` (stages a new InputSystem before replacing current); \`readProfile\`, \`writeProfile\`, \`applyProfile\` (validates every target id before rebinding). Uses the existing \`engine/data/Json.h\`, string key/button/axis names from platform enums, limits on objects/counts/numerics and helpful path errors. **Host/presentation only**, not linked into core deterministic ECS.
- \`apps/fps/client.cpp\`: loads optional complete \`<project>/input/actions.json\` (invalid or missing -> built-in mapping fallback), then local profile under \`platform::userDataDir()/enhanced-input-<stable FNV path hash>.json\`. \`App::rebindInput(context,id,Control)\` is a local host/UI hook that updates the profile and writes a temp+rename file; \`platform::syncUserData()\` after write. No user-visible binding editor yet.
- \`tests/libraries/enhanced_input_config_tests.cpp\` and its CMake target/CTest \`library_enhanced_input_config\`: JSON asset/profile roundtrip, chord config, stable code names, unknown IDs/duplicate bindings/unsupported code/version fail closed.
- \`docs/enhanced-input.md\`: user-facing architecture, examples, complete JSON schema, profile format, native/ECS usage and validation.
- \`skills/ui/aiverse-input/SKILL.md\`: rewritten for Action/Context API and requires a doc handoff after each step.
- \`libraries/catalog.json\`: existing input package 0.1.1 -> 0.2.0, dependency pins for new headers/docs and descriptions. Legacy source remains \`gameplay/Input.h\`.
- \`AGENTS.md\`: added Enhanced Input ownership row and per-stage agent handoff rule.

**Supported contract:** asset path is a convention for the standalone FPS sample, not an implied existing \`rules.json\` or \`game.json\` option. If present, the asset contains the **entire** desired FPS action set (not a partial merge). Pure header can be consumed by a native C++ project via \`AIVERSE::lib_input\`. The platform JSON adapter requires engine JSON includes. Keep \`input/actions.json\` inside each concrete project's own repo, not in the engine repo.

**Verification status:** implementation and documentation committed; Linux/Apple/MSVC compiler and CTest have not yet returned success. Draft PR #20 triggers CI; prior CI runs were canceled due new commits. Explicitly investigate final CI results and do not merge until green. The repo is too large to pull in the current tool container (network DNS is unavailable); validate through Github Actions and user's local Mac build.

**Next agent: Stage 5**
1. Fix any compiler/test failures from draft PR #20 CI. Check new header for GCC/MSVC aggregate warnings and \`tests/libraries/CMakeLists.txt\` nesting. Run isolated library targets and full engine build.
2. Add editor input settings (Input Actions / Mapping Contexts / modifier/trigger UI, capture key to rebind, priority and conflict warnings), using \`docs/editor-windows.md\` and ImGui/RmlUi event capture correctly.
3. Integrate RTS \`apps/client\` and in-editor hosted game input with higher-priority UI/Modal/Viewport contexts, preserving text input/IME and the existing RTS Commands. Treat gameplay actions as client->Commands, not sim mutations.
4. Test pad disconnect, held key during modal activation, duplicate explicit IDs, chord consumption precedence, same-frame tap, focus loss; update reference docs, tests, skill/catalog and this handoff after the next stage. Keep unrelated editor changes minimal.

## Stage 5 — inspector FPS/ECS merge (2026-11-18)

**Integrated parents:** Enhanced Input `247d9fb4344abf9b7988e367ce83f31a9afe6863` and `feature/universal-entity-inspector-fps` `26eb4d914fc65f336c9e843db198f2316f6f14a2`. Two-parent merge preserves both histories. Resolved tree is inspector HEAD plus Enhanced Input files, not a reset to `main`.

**Conflict resolutions:** `AGENTS.md` retains inspector's ECS/renderer rules and adds input ownership and stage-by-stage handoff. `libraries/catalog.json` updates only `input` package 0.2.0 while retaining all inspector library entries. `tests/libraries/CMakeLists.txt` retains inspector test targets and appends three enhanced input test targets, dropping the redundant additional `endif() # voxel packages` from the input branch. FPS client kept from input branch; inspector did not modify that path after common base. Do not overwrite the merged inspector/editor or ECS sources with outdated main.

**Validation:** source and tree comparison over GitHub connector; no native compile or CTest yet (build container cannot resolve GitHub DNS). Run CMake/CTest on the merged branch, audit the FPS client against current `modules/fps`, then add editor/RTS contexts and runtime binding settings. Update this handoff after each stage before proceeding.

## Stage 6 — runtime ID / touch hardening (2026-11-18)

**Changes:** `EnhancedInput.h::InputSystem::registerContext` now rejects duplicate explicit mapping IDs, unknown referenced action names and invalid MAX ids before modifying registered contexts. Automatic mapping IDs skip all authored IDs. `EnhancedInputAdapter.h` reuses freed touch slots (0–31), ignores repeated begin for the same touch ID and ignores fingers over the supported 32-slot maximum without wrapping. `enhanced_input_tests.cpp` adds registration regression checks.

**Compatibility:** legacy `gameplay/Input.h` is unchanged. Action values, FPS/RTS network Commands and profile schema 1 are unchanged. Native C++ projects may continue linking `AIVERSE::lib_input`.

**Verification:** source-level assertions reviewed; merged CI was queued when checked. No compiler or CTest run yet. Continue checking GitHub Actions or run the isolated `aiverse_enhanced_input_tests` on a machine with the checkout.

**Next agent:** finish the editor Input Actions/Contexts UI and integrate safe project asset editing, then migrate RTS/hosted game UI arbitration and verify. Add a document section after each stage.

## Stage 7 — editor authoring window (2026-11-18)

**Code:** Added `apps/editor/windows/InputWindow.cpp` registered as window ID `input` in the existing Logic group. Opens the project's optional `input/actions.json`; supports Actions and typed values, context enable/priority, mappings, key capture, modifiers/triggers, and warnings for duplicate controls. Saves only schema-valid full assets via `enhanced_config::writeAsset/readAsset`; temporary write and backup file protect existing JSON. Existing per-user override profiles remain separate. `EditorWindow::captureGlobalShortcuts` and `Editor::handleShortcuts` suppress F5/undo while key capture is armed; `update` cancels capture when dock tab loses front position. `CMakeLists.txt` links `aiverse_editor` directly to `AIVERSE::lib_input`; the existing editor window GLOB picks up the new source.

**Files:** `apps/editor/windows/InputWindow.cpp`, `apps/editor/EditorWindow.h`, `apps/editor/Editor.cpp`, root `CMakeLists.txt`, `docs/enhanced-input.md`, this handoff.

**Validation:** GitHub source updates only; no compiler/CTest results. Verify ImGui named key enum range and project asset backup/rename path with Clang/macOS, GCC/Linux and MSVC/Windows. Ensure Input Actions window works with FPS and RTS; remapping F5 must not begin Play. The editor does not generate complete FPS defaults automatically, so users must build the full scheme when creating an asset.

**Next agent:** migrate the common RTS client hotkeys to semantic actions, preserve `scriptCommandKey`/control groups/hosted view UI handling, add focused tests and run full CTest/CI before marking complete. Update this handoff after Stage 8.

## Stage 8 — RTS semantic shortcut migration (2026-11-18)

**Code:** `apps/client/ClientInputRouting.h` defines default semantic Actions `AttackMove`, `Patrol`, `Repair`, `Reclaim`, `Stop`, `Hold`, `FocusCamera` under Mapping Context `RTS.Gameplay`. The handler accepts platform KeyDown/KeyUp only, raises `Triggered` events, and immediately cancels held state when RmlUi/modal/text/paused UI captures input, or on focus loss. `GameClient::event` now consumes those semantic actions instead of hardcoded A/P/R/E/S/H/Space, preserving `scriptCommandKey` precedence and the existing command implementations. `GameClient::initStep(0)` optionally loads a full project-authored `input/actions.json` into that same local input system, falling back to defaults on a parse/config error. Core simulation, FPS network input and RTS Command wire formats are unchanged. Control groups, selection pointer gestures, WASD camera scrolling and other specialized client inputs remain on their current paths for compatibility.

**Files:** `apps/client/ClientInputRouting.h`, `apps/client/GameClient.h`, `apps/client/GameClient.cpp`, `tests/libraries/enhanced_rts_input_tests.cpp`, `tests/libraries/CMakeLists.txt`, `docs/enhanced-input.md`, this handoff.

**Test cases added:** pressed-only RTS actions, KeyUp and hold/repeat, UI modal/focus cancel and higher-priority context overriding a shortcut. Full build/CTest still pending, and a real RTS example should verify script keys, ctrl groups and hosted-client input.

**Next agent:** complete default camera/mouse action migration where it is safe, add user-facing rebinding profile UI (separate from authored assets), and verify compiler, CTest, macOS hosted FPS/RTS and other platforms. Never claim end-to-end validation from source-level review.

## Stage 9 — refresh inspector branch after concurrent ECS/test changes (2026-11-18)

`feature/universal-entity-inspector-fps` advanced by 8 commits after the Stage 5 merge, from `26eb4d9` to `dd711f5`. Compared all 13 changed paths: ECS scene hierarchy, projectile systems/tests, engine test runner, skills catalog, validation workflows and developer docs. None overlaps this input feature's files. Applied those 13 newer paths on top of the current Enhanced Input worktree as a second two-parent merge, retaining both parents and input changes.

**Verification:** GitHub API changed-path comparison; zero overlapping paths, no local CMake/CTest result; CI remained queued during source review. This merges only the known 8 new upstream commits; repeat comparison before PR finalization if the inspector branch moves again.

**Next agent:** compile editor/FPS/RTS input targets, run all library input tests and inspect CI. Continue parity work for camera/pointer/gamepad actions only after capturing results in this handoff.

## Stage 10 — in-game FPS rebinding/settings (2026-11-18)

**Implementation:** The standalone FPS reference client now forwards platform events to its existing ImGui layer and provides an `Input Settings` panel toggled with F10. Opening releases pointer lock and cancels held gameplay input. The panel enumerates all registered Mapping Contexts (including inactive), shows semantic Action-to-Control bindings, collision notices and live key/mouse/gamepad capture. Rebindings call the existing `App::rebindInput`, persisting only user profile overrides; authored `input/actions.json` stays unchanged. Escape cancels capture/ closes settings, F10 toggles, clicking the viewport reacquires pointer lock. Added the read-only `InputSystem::registeredContexts()` enumeration API and a core test assertion.

**Files:** `apps/fps/client.cpp`, `libraries/include/aiverselib/gameplay/EnhancedInput.h`, `tests/libraries/enhanced_input_tests.cpp`, `docs/enhanced-input.md`, this handoff.

**Validation:** code/asset API inspected; FPS visual/interactive behavior and compilation have **not** been verified. Check F10 pointer lock, keyboard+mouse capture, pad buttons and axes, profile reload, duplicate mapping conflicts, and focus loss on macOS and web. The UI intentionally does not modify project assets.

**Next agent:** review CI failures and run CMake/CTest, complete any remaining RTS camera/selection mappings and hosted-game input focus gating. Decide whether mobile touch gesture profiles need dedicated schema in a separate feature. Do not mark this PR ready without green CI.

## Stage 11 — explicit link wiring and agent skill refresh (2026-11-18)

**Changes:** root `CMakeLists.txt` now links `AIVERSE::lib_input` into both desktop `aiverse_client` and web/mobile `main` target, so `apps/client/ClientInputRouting.h` and `EnhancedInputConfig.h` resolve the public `<aiverselib/...>` include path in all build variants. Editor target already links the library. `skills/ui/aiverse-input/SKILL.md` now routes agents to the editor authoring UI, FPS player settings UI and RTS shortcut adapter; mandatory input validation now covers `library_enhanced_rts_input`.

**Verification:** GitHub connector source audit only. CI on last commit was queued at check time; compilation and CTest remain unverified. Do not mark these links compile-verified before CI/local build completes.

**Next agent:** check CI logs, run `aiverse_enhanced_input_tests`, adapter/config/RTS tests plus `aiverse_editor`, `aiverse_client`, `aiverse_fps_client`; test interactive rebind and hosted GameWindow/Scene focus. Fix compiler/warning failures immediately, append another handoff stage.

## Stage 12 — shared FPS/RTS presets and editor initialization (2026-11-18)

**Change:** `engine/platform/EnhancedInputPresets.h` now provides `enhanced_presets::fpsReference()` and `rtsReference()` producing complete version-1 `enhanced_config::Asset` instances with explicit stable IDs. FPS client `configureInput()` installs the exact same FPS preset; RTS `ClientInputRouting` installs the shared RTS preset. Editor Input Actions window adds **Use FPS defaults** and **Use RTS defaults** buttons, allowing agents/users to create complete mapping assets without forgetting default actions. Buttons replace the in-memory editor buffer only; Save persists them. Previous hardcoded FPS/RTS schemes were removed from the clients, preserving their semantic actions and order.

**Files:** `engine/platform/EnhancedInputPresets.h`, `apps/fps/client.cpp`, `apps/client/ClientInputRouting.h`, `apps/editor/windows/InputWindow.cpp`, `tests/libraries/enhanced_input_config_tests.cpp`, `libraries/catalog.json`, `docs/enhanced-input.md`, `skills/ui/aiverse-input/SKILL.md`, this handoff.

**Tests:** Added preset JSON write/read/install roundtrips and stable mapping ID assertions (ID 1..N) to `enhanced_input_config_tests.cpp`. Validation is not yet backed by a compiler or successful CTest; GitHub CI was queued. Run tests before promoting PR #20 out of draft.

**Next agent:** check CI errors, verify initialized FPS key controls parity and mouse look, verify project preset editing/saving plus runtime profiles and RTS UI focus, then run full cross-platform validation.

## Stage 13 — mobile touch movement axes and multi-touch safety (2026-11-18)

**Changes:** `EnhancedInputAdapter.h` now tracks each finger's previous position and emits accumulated per-frame X/Y motion through `Device::Touch` codes 1 and 2 (code 0 remains touch-down). Slot allocator reuses free indices 0..31; duplicate TouchBegin ignored; TouchEnd releases and can emit final delta, TouchCancel only releases, FocusLost clears everything. No device-specific backend change. The InputWindow numeric Touch code field can already author channels 0,1,2; JSON validates device indices.

**Test additions:** `enhanced_input_adapter_tests.cpp` now verifies two coalesced TouchMove events, transient movement completion, duplicate begin and slot recycling, TouchCancel release.

**Verification:** committed source/test changes; no running CTest or live iOS/Android verification. Need capture multi-touch motion on physical devices and hosted editor events, especially when UI owns touch.

**Next agent:** finish host/view focus arbitration and cross-platform build; run all tests, review modifiers/triggers for repeat-state parity, and record CI and manual checks in handoff before ending.

## Stage 14 — hosted editor Game window focus handoff (2026-11-18)

**Problem:** switching Game window to Inspector/Input Actions/editor text was not an OS FocusLost. The host stopped forwarding new keys, but previously held WASD and semantic actions stayed held and could move the game unexpectedly.

**Fix:** `apps/editor/ClientHost.h/.cpp::place` detects focus transitions and sends synthetic `FocusLost`/`FocusGained` to the hosted `GameClient` under its existing `Scope` (HostedView + ImGui state restored). Focus loss releases captured touch/pointer. Start/stop reset host focus. `apps/client/GameClient.cpp::event` also clears the old `keys_` array on FocusLost; semantic InputSystem::cancelAll is handled by `ClientInputRouting`.

**Build/testing:** code updates only, not yet compiled or manually verified; CI pending. Test Game→Inspector→Input Actions→Game with held WASD, RmlUi modal, touch dragging, and scoped context restoration. Network Command schemas unchanged.

**Next agent:** compile and run CI and manual hosted game tests, then document exact results.

## Stage 15 — upstream inspector sync and verification gate (2026-11-18)

`feature/universal-entity-inspector-fps` advanced one additional docs-only commit, `cf650a02d22890cbc3b27280762e8c0d96502bed`. The new `docs/editor-unification-handoff.md` was merged as-is with the Enhanced Input feature changes; no ECS/API conflict. PR #20 targets the inspector branch so reviewers see an input-focused diff, not the entire ECS history.

**Current validation gate:** the latest input-head GitHub workflows were `queued` for CI, AIVERSE Studio and ECS experimental at inspection time; older feature-head CI runs were canceled by newer commits. A complete CMake/CTest run was not available: the local isolated container cannot resolve github.com to clone AIVERSE, and the connected GitHub service only accesses source/CI metadata. Do **not** report green build, Apple Silicon integration success or all platforms verified. Keep PR draft until compilation/tests/manual smoke checks pass.

**Required local commands (from AIVERSE checkout):**

```sh
git fetch origin
git switch feature/aiv990_enhanced_input
git pull --ff-only origin feature/aiv990_enhanced_input
cmake -S tests/libraries -B build/enhanced-input-check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/enhanced-input-check --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests
ctest --test-dir build/enhanced-input-check --output-on-failure -R 'library_enhanced_(input|rts_input)'
python3 tools/libraries/catalog.py check
cmake --preset macos
cmake --build build/macos --config Release --target aiverse_editor aiverse_client aiverse_fps_client
```

**Smoke-test checklist:** editor opens Input Actions, selects FPS/RTS defaults and saves valid `input/actions.json`; FPS F10 rebinding persists across restart; gamepad/mouse/touch modifiers and profile IDs remain stable; Game→Inspector tab focus releases held RTS movement; RTS script-command overrides and network Commands unchanged. Run all AGENTS.md engine regression and determinism suites before merge. Cross-platform macOS/Linux/Windows/Web/Android/iOS remain unverified.

**Next agent entry point:** read this document and `docs/enhanced-input.md`; obtain build logs/CI results, fix warnings and failures, then write Stage 16 with exact test evidence. Avoid re-implementing the input core; remaining high-level gestures and non-hotkey RTS controls can follow as additive stages.

## Stage 16 — transactional user profile write/rebind (2026-11-18)

**Problem:** FPS `rebindInput` previously modified `actions` and `inputProfile` before saving; failed file writes could leave runtime keys changed without a matching durable profile. Existing Windows overwrite fallback also removed the original file before confirming replacement.

**Fix:** new `engine/platform/EnhancedInputProfileIO.h` provides `enhanced_config::loadProfileFile/saveProfileFile` with validated JSON, temporary writing, atomic POSIX replacement and backup/restore fallback for Windows. FPS stages both a copy of the current `InputSystem` and user `Profile`, validates/applyProfile into staging, persists the profile, then swaps the in-memory state and calls `syncUserData`. Invalid controls, unknown mapping IDs and disk errors leave the prior live mapping untouched. FPS initial profile load also uses staging, so invalid stored overrides are ignored without affecting defaults.

**Tests:** `enhanced_input_config_tests.cpp` adds host-file roundtrip plus failed invalid-profile overwrite retention using a temporary file. Runs on the existing `library_enhanced_input_config` target; test execution pending.

**Verification:** source changed and committed, CI jobs were still queued at inspection time, no local full checkout due blocked GitHub networking. Do not claim a passing build. Next agent: compile and run this test on Windows and POSIX, then check editor and RTS focus/profile parity before PR readiness.

## Stage 17 — two-finger pinch and centroid pan (2026-11-18)

**Code:** `EnhancedInputAdapter::updateGesture` recognizes exactly two active touches, sampling distance and centroid. `Device::Touch` control codes 3 (pinch distance delta), 4 (midpoint pan X), and 5 (midpoint pan Y) publish frame-accumulated relative values on `deviceIndex=0`. Re-baselines when a second finger joins, fingers leave/cancel or focus is lost, so no jump is emitted from contact changes. Per-finger 0/1/2 codes remain backward compatible. Add standard Action mappings to these codes in any project, including user-authored JSON.

**Tests:** `enhanced_input_adapter_tests.cpp` verifies two-finger activation without ghost delta, pinch +5, centroid pan -2.5, release completion, no jump on replacing a finger and focus-loss reset. Compile and CTest not yet verified.

**Limits:** gestures are input signals (not simulation commands), and UI must capture/fence touches first. Rotation gestures and multi-finger gestures other than exactly two are not implemented. The existing genre-specific RTS region gesture remains unchanged until it is migrated.

**Next agent:** verify builds and add a reference action-driven camera path; integrate RTS pointer/zoom and editor GameWindow where current behavior can be preserved, then update docs.

## Stage 18 — RTS camera actions and lifecycle cleanup (2026-11-18)

**Implementation:** Extended `enhanced_presets::rtsReference()` with `CameraPan` (arrows) and `IdleCameraPan` (WASD when selection is empty), both `Axis2D` Actions in a new `RTS.Camera` context. Camera bindings are non-consuming so gameplay A/P/R/E/S/H actions remain available. Existing `RTS.Gameplay` mapping IDs 1..7 remain untouched for rebinding profile compatibility. `apps/client/GameClient.cpp::frame` now samples semantic camera axes rather than raw `keys_` for keyboard camera pan; gamepad and pointer-edge movement remain unchanged. `ClientUi.cpp::setScreen` clears semantic input when transitioning away from Playing to prevent held keys on pause/menu.

**Tests:** `enhanced_rts_input_tests.cpp` now asserts arrows/idle camera analog values, action coexistence and dual contexts. No on-device/CI verification yet.

**Behavior:** Axis mapping is combined with original gamepad and edge scroll, clamped per direction to preserve normal speed (identical arrow+W no longer doubles speed, intentional). Pointer selection, project scripts and top-down possession still have their specialized input, preserving game rules and commands. Project `input/actions.json` is a complete replacement and must define camera bindings explicitly if desired.

**Next agent:** migrate compatible editor GameWindow camera controls, verify old RTS movement/selection and keyboard focus, run CMake+CTest for library RTS input and native clients, then log results.

## Stage 19 — editor GameWindow camera routing (2026-11-18)

**Implementation:** `apps/editor/windows/GameWindow.cpp` now installs the shared RTS semantic camera mapping (or the project's `input/actions.json`) into a presentation-only `InputSystem`. It polls only the declared keyboard controls through ImGui when the GameWindow is focused and no text field owns input; unfocused/text capture calls `cancelAll()`. Camera movement uses the resulting `CameraPan` + `IdleCameraPan` values instead of direct WASD/arrow conditionals. The project asset is rechecked at most twice per second while the GameWindow is drawn, so edited bindings become active without restarting. Direct FPS projects and hosted GameClient continue their separate input paths.

**Compatibility:** The editor's direct RTS play historically allows WASD camera pan regardless of selected units; unlike the standalone RTS client, both Axis2D actions are combined unconditionally. Pointer/selection gestures, regions and GameHud retain their specialized handling. No authoritative ECS state, gameplay Commands or serialized command layout changed.

**Verification:** GitHub source changes only, no full editor compile/interactive test yet. A focused validation must check ImGui key enum mappings, window focus cancellation, readAsset load/reload and project input hot-rebind. Test selection + keyboard camera, script HUD popup, region camera and initial missing input file fallback.

**Next agent:** check CI and compiler outputs; fix any editor/RTS input warnings and verify functional parity. Do not mark PR ready without green validation. Continue documenting new stages.

## Stage 20 — validated editor project asset writes (2026-11-18)

**Problem:** `InputWindow::save` previously removed an existing `input/actions.json` before renaming the temporary file. On Windows or filesystem errors this could lose user-authored mappings.

**Fix:** New `engine/platform/EnhancedInputFileIO.h` provides schema-validated `loadAssetFile/saveAssetFile` and the shared `detail::saveValidatedJsonFile` writer (atomic POSIX replace or Windows backup/restore fallback). `EnhancedInputProfileIO.h` reuses the same writer. `apps/editor/windows/InputWindow.cpp` now loads/saves via the checked library instead of handwritten destructive file operations. The UI still rejects empty Actions/Contexts and retains its last in-memory buffer on errors.

**Test coverage added:** `enhanced_input_config_tests.cpp` now creates a project action asset in a temporary file, validates roundtrip, attempts invalid replacement and verifies the previous data stays intact. The pre-existing user-profile roundtrip/failure tests remain.

**Source/test status:** GitHub source committed; CMake/CTest and macOS/Windows GUI verification remain pending. Need check user asset read-only errors and project-switch/reload behavior on actual OS. Both file APIs are presentation-only; no sim/ECS format changed.

**Next agent:** inspect CI and build; debug errors; run UI focus/rebind/camera functional test and full AGENTS.md validation before making PR ready.

## Stage 21 — RTS semantic pointer controls / wheel zoom (2026-11-18)

**Implementation:** `enhanced_presets::rtsReference()` adds `RTS.Pointer` Mapping Context with `Select`, `Command` Boolean press Actions and `CameraZoom` Axis1D on mouse wheel Y, all non-consuming and assigned stable mapping IDs 1..3. `ClientInputRouting` owns `platform::EnhancedInputAdapter` and forwards keyboard/pointer/touch events through the shared runtime **after UI pointer ownership is resolved**. Its `reset` handles Touch/held input through the adapter. `GameClient::event` now derives ordinary camera wheel zoom from semantic `CameraZoom` value instead of raw `e.wheelY`. Pointer selection/drag/garrison/tap semantics still execute their existing high-level logic, but projects and agents can bind to `Select`/`Command` Actions without duplicating low-level device conversion. RTS native command format unchanged.

**Tests:** `enhanced_rts_input_tests.cpp` verifies CameraZoom wheel value and UI blocking plus Select click; registered context count now 3. Real editor/gamepad/multitouch behavioral validation still pending.

**Next agent:** assess code/CI diagnostics, compile and test the complete suite, and document any remaining native/Windows/Web issues. Do not replace RTS selection algorithms with simple button clicks: selection is a distinct high-level mechanic and stays presentation-side.

## Stage 22 — hidden hosted Game tab input isolation (2026-11-18)

**Problem:** `Editor::event` forwarded every native input event to `ClientHost`. A hidden Game dock tab might stop drawing before `ClientHost::place(...,focused=false)` ran, so stale `focused_` or `captured_` could continue forwarding keyboard and pointer input while the user edits an Input Action/Inspector field.

**Fix:** `apps/editor/ClientHost.h/.cpp` adds `suspendInput()` to send hosted `FocusLost` and clear keyboard/pointer/touch capture without requiring a draw. `Editor::event` checks that the Game tab is open and in front; a hidden client is suspended and receives no further ordinary events. Real OS `FocusLost` remains forwarded to clean up the host. This is in addition to Stage 14's synthetic focus edges in `ClientHost::place` for ordinary focus changes while Game is drawn.

**Files:** `apps/editor/ClientHost.h/.cpp`, `apps/editor/Editor.cpp`, `docs/enhanced-input.md`, this handoff.

**Verification:** GitHub source committed; no macOS GUI runtime test or CTest result. Verify two dock tabs (Game/Input Actions) while holding WASD, dragging from an embedded game screen, text entry, switching back/forth and global FocusLost while hidden. Input must not leak, duplicate clicks or remain stuck.

**Next agent:** check all CI jobs, build editor, client and FPS; run interaction smoke tests, document exact verified evidence, and then consider merge approval.

## Stage 23 — refresh inspector parent and release validation snapshot (2026-11-18)

`feature/universal-entity-inspector-fps` advanced from `cf650a02d22890cbc3b27280762e8c0d96502bed` to `4a1173ce26a7f09c7fe0438755d28f55a7a1233b` (two docs-only commits). Its updated `docs/editor-unification-handoff.md` and new `docs/agent-handoffs/f90242ec4-integration.md` were carried forward in a two-parent merge with the current input feature branch. No overlapping C++ or test changes were present. Repeat the comparison just before PR merge because the inspector branch may still move.

**Validation snapshot:** all four targeted CTest executables are registered, and GitHub source was audited for link paths/brace balance; CI had not yet returned green results when inspected. Previous input branch workflow runs were often canceled by subsequent commits, and later runs remained queued. A full macOS/Windows/Linux/Web/Android/iOS build, editor runtime smoke test and complete engine determinism suite have NOT been completed in this environment. The GitHub-enabled connector can update code but cannot execute a compiler; the working container has no GitHub network route for a complete clone. Never claim tests passed or mark PR ready based on only source inspection.

**Next agent:** before merging PR #20, run the exact commands in Stage 15, inspect CI job logs, verify project asset writes (invalid JSON, read-only paths), FPS F10 rebinding across restart, RTS camera/zoom, Game vs Input Actions dock focus isolation and two-finger gestures on actual devices. Append verified build/test evidence as Stage 24. Keep the PR draft until then.

## Stage 24 — Action-level modifiers gate lifecycle (2026-11-18)

**Bug:** `InputSystem::update` previously marked an Action active and `Triggered` based on pre-Action aggregate input. `Action::modifiers` (such as DeadZone or Scale(0)) and `ValueType` constraints could then produce a zero Action Value while still emitting Started/Ongoing/Triggered. This violates the typed-action pipeline.

**Fix:** `EnhancedInput.h` computes the post-modifier constrained Action Value before updating action lifetime state. `ActionState.active`, Started/Ongoing/Completed and default trigger eligibility follow that final value. Release/one-frame Tap pulses still work even when the released value is zero. Mapping-level trigger timing and network command formats are unchanged.

**Regression:** `enhanced_input_tests.cpp` now covers a sub-threshold Axis1D Action-level dead zone, a Scale(0) disabled Action, a threshold crossing that starts action, and crossing back below threshold that completes it.

**Validation:** source and tests committed, no execution results from full project CI yet. Run `library_enhanced_input` and FPS input+gamepad integration before merging. Next agent should check composite chord and same-frame tap regressions, record actual compiler/test results in Stage 25.

## Stage 25 — independent cross-platform Enhanced Input CI gate (2026-11-18)

**Change:** new `.github/workflows/enhanced-input.yml` runs a focused C++20 library regression matrix on `ubuntu-24.04`, `macos-15`, `windows-2022`. It configures the existing standalone `tests/libraries/CMakeLists.txt`, builds four targets (`aiverse_enhanced_input_tests`, `aiverse_enhanced_input_adapter_tests`, `aiverse_enhanced_input_config_tests`, `aiverse_enhanced_rts_input_tests`) with warning-as-error settings already set by CMake, and runs their CTest names. Push, PR changes in input-related files and explicit manual workflow dispatch can trigger it.

**Why:** the monolithic engine CI was still queued after multiple incremental commits. The focused job has a much smaller compile surface than complete editor/FPS packages, but does not replace full platform CI and macOS visual play-test.

**Validation:** workflow committed; GitHub Actions results not yet completed at writing. Watch the matrix for compiler warnings, link errors, test assertions and Windows filesystem backup behavior. The current feature remains a draft PR, pending green results.

**Next agent:** inspect `Actions → Enhanced Input targeted checks` and fix failures; run full editor/client/FPS builds, engine regressions and manual input/hosted viewport smoke tests. Add Stage 26 with exact per-platform PASS/FAIL evidence before marking PR ready.

## Stage 26 — deterministic binding conflicts and rebind policies (2026-11-18)

**Core:** `lib::gameplay::enhanced::RebindPolicy` is `Allow` (backwards-compatible default), `Reject` (fail if another mapping in the same context already uses the target control), or `Swap` (exchange controls with exactly one conflicting mapping, preserving stable IDs; reject multi-way ambiguous collisions). `InputSystem::conflicts(activeOnly=true)` enumerates all same-control collisions in actual context dispatch precedence, identifies the two context/actions/mapping IDs, and flags when the earlier consumer shadows the later mapping. It ignores inactive contexts unless asked explicitly.

**Tests added (not run):** `tests/libraries/enhanced_input_tests.cpp` exercises Reject/Swap/Allow, ID preservation and priority/shadowing report. Existing `rebind(context,id,control)` callers retain their previous Allow semantics.

**Why:** two Actions sharing the same physical button is often intentional (e.g., tap vs hold or gameplay versus modal UI). Automatically deleting duplicates would silently corrupt authored input schemes. Agents should query `conflicts()` and offer users a choice, never mutate mappings without an explicit policy.

**Files:** `EnhancedInput.h`, unit test, `docs/enhanced-input.md`, this handoff. **Validation:** postponed at user request; no compiler or runtime evidence yet. Next stage: expose conflict resolution in Input Actions editor/FPS Settings and document.

## Stage 27 — advanced Unreal-style triggers (2026-11-18)

New cross-genre Trigger types: `DoubleTap(seconds)` (second press after a preceding release within a bounded interval); `HoldAndRelease(seconds)` (release after sufficient hold time); `Pulse(seconds)` (periodic trigger while held, min 10 ms); `NotChord(action)` (inverted chord blocker; required action must be inactive). Core maintains per-mapping runtime release window/pulse timing and continues to accumulate same-frame inputs. JSON V1 recognizes `doubleTap`, `holdAndRelease`, `pulse`, `notChord`; encoder writes matching values. Editor Input Actions trigger dropdown/seconds/chord selection supports all ten types. Chord validation also checks the inverted blocker.

**Files:** EnhancedInput.h, EnhancedInputConfig.h, InputWindow.cpp, enhanced_input_tests.cpp, this handoff and docs. Tests authored for double-tap, hold release, pulse, inverted chord; execution intentionally deferred at user's request.

**Limitations / next:** the new types are additive, preserving all existing enum ordinal values and JSON syntax. Advanced timing is per active mapping/context, not saved in the deterministic sim. Review repeated activations after context changes and cyclic complex chord definitions during later validation.

## Stage 28 — multi-local-user input ownership (2026-11-18)

**New reusable API:** `engine/platform/EnhancedInputUsers.h`, `platform::EnhancedInputUsers`, owns independent Enhanced Input core/adapter per local `UserId` (not an ECS entity). Add user with `addUser(id, InputSystem scheme)`; assign keyboard/pointer/touch individually with `setKeyboardOwner`, `setPointerOwner`, `setTouchOwner`, and physical gamepad with `assignGamepad(pad,id)`. `onEvent(event, gameplayAllowed)` forwards only to the rightful user after UI capture; `pollGamepads()` polls hardware **once**, samples each assigned physical pad into the user's logical `deviceIndex=0`, and `update(id,dt)` returns only that user's Action events. Ownership transfers, unassignment, UI capture and OS FocusLost cancel held Actions; one pad per user is enforced. A project may create multiple simultaneous local players (split-screen, couch co-op); no shared static keyboard state or genre restriction.

**Compatibility:** Existing `EnhancedInputAdapter`/single-user FPS/RTS APIs are unchanged. New router is optional; avoid installing it in a single-user game if there is already a platform adapter polling the same hardware. For local-player gamepads, author logical gamepad index **0** per player; this is intentionally independent of physical pad IDs. `EnhancedInputUsers` lives in platform presentation code, not deterministic ECS.

**Tests added (deferred):** `enhanced_input_adapter_tests.cpp` checks isolated keyboard vs pad events, assignment validation, canceled pad state on detach and focus cleanup. `libraries/catalog.json` and the input AI skill include the new reusable native module.

**Next:** connect to optional split-screen sample/preset and expose player-specific profile storage; then continue top-down client migration. Runtime validation and CTest explicitly deferred by user.

## Stage 29 — top-down combat/movement migration (2026-11-18)

**New preset context:** `RTS.TopDown` (priority 100, inactive until a top-down hero is controlled) supplies semantic Actions `TopDownMove` Axis2D (WASD/arrows), `TopDownFire` (Space/right mouse), `TopDownAim` (left mouse), `TopDownEnter` (F/Enter), `TopDownWeaponNext` (X/Tab), `TopDownWeaponPrev` (Z), and `TopDownWeapon1..9` (number keys). Existing RTS.Gameplay/RTS.Camera/RTS.Pointer mapping IDs are unchanged. `ClientInputRouting::setTopDownEnabled` activates/deactivates this context without mixing genres into the core library.

**Client:** `GameClient::event` updates that context before normal event routing. `GameClient::topDownEvent` now consumes semantic one-shot Actions for weapon cycling, weapon slots and entering vehicles (legacy key fallback if a project-supplied scheme lacks RTS.TopDown). `topDownFrame` samples semantic `TopDownMove`, `TopDownFire`, `TopDownAim` from existing input state, preserving keyboard fallback for older action schemas. It still sends existing `kCmdTopDown` / arsenal Commands and continues using touch-stick/aim assistance/prediction calculations and project-specific ability buttons from `TopDownControls`.

**Scope limits:** project-authored `input/actions.json` is a full replacement: rebind named TopDown Actions there to customize the hero. Dynamic game-specific ability button keys and commerce script keys still use their explicit game.json/script mapping rules; these are not interchangeable generic camera/weapon actions. Keyboard is captured after UI filtering. Run top-down regression after user-requested implementation phase.

**Next:** expose conflicts and policies in editor FPS settings, improve per-player profiles, map RTS touch gesture deltas through Actions, then perform static review and defer actual CTest/CI until requested.

## Stage 30 — conflict policies usable in editor and FPS settings (2026-11-18)

**Editor:** `InputWindow` now has `On capture conflict` (Allow duplicate / Reject duplicate / Swap keys). Keyboard-capture uses that policy, preserving IDs and swapping exactly one previous binding when requested; ambiguous multi-way collisions give an explicit error. It also shows `InputSystem::conflicts()` across active Mapping Contexts with higher/lower contexts, Action names, stable mapping IDs, and shadowing information.

**FPS:** F10 `Input Settings` has the same three conflict policies, default Swap, plus a cross-context conflict viewer. `rebindInput(...,RebindPolicy)` stages a copy of the whole input system. In Swap mode it records **both affected mapping IDs** into a staged validated user profile, verifies the profile by applying it to a copy of the current system, persists to disk, then commits the runtime state. A failed Reject/ambiguous Swap/validation/disk save leaves both live input and profile unchanged and reports an explicit error.

**Files:** `apps/editor/windows/InputWindow.cpp`, `apps/fps/client.cpp`, this handoff and user guide.

**Unverified:** code committed; actual compilation, F10 UI, and profile restart/Swap smoke tests intentionally deferred until user testing phase. Need verify ImGui combo labels, capture and syncUserData on real macOS/Windows.

**Next:** per-user player profiles and optional Action-based RTS touch camera gestures, then source-level closure audit. No tests run yet.

## Stage 31 — shared per-user profile store, FPS migration and authored preset test fix (2026-11-18)

**New API:** `engine/platform/EnhancedInputUserProfiles.h` (`enhanced_config::UserProfiles`) provides `setScope(userDataDir,projectIdentifier)`, `path(userId)`, `load(userId,InputSystem)`, `rebind(userId,InputSystem,context,id,control,policy)`, and `reset(userId,InputSystem,projectDefaults)`. Profiles are isolated by project and numeric local-user ID. File hashing is stable FNV-1a; user 0 retains the existing FPS `enhanced-input-HASH.json` name for backwards compatibility; users 1+ use `-user-<id>`. All writes validate first, preserve previous state on failure and persist **both IDs** for Swap.

**FPS integration:** `apps/fps/client.cpp` now uses `UserProfiles` for load/rebind instead of duplicating per-profile storage logic; project-authored defaults are cached before applying player 0 overrides. F10 UI includes `Restore project defaults`, which resets the user's profile without changing `input/actions.json`, and uses the shared transactional persistence. Still a single network/local player in FPS sample; the multi-user service is available for native split-screen projects.

**Tests authored and obvious stale assumptions fixed (execution deferred):** `enhanced_input_config_tests.cpp` no longer assumes RTS has exactly one context (it grew Gameplay/Camera/Pointer/TopDown); checks all contexts' stable IDs. New profile tests cover two isolated player paths, swapping two FPS keys, restart/restore and reset to defaults. `enhanced_rts_input_tests.cpp` expects four default RTS contexts. Catalog and agent skill updated.

**Next:** close remaining RTS touch/gesture routing and player UI input focus; inspect source/CI only, no tests until user requests test phase.

## Stage 32 — RTS single- and two-finger gestures via Actions (2026-11-18)

**New default context:** `RTS.Gestures` (active/non-consuming) defines `CameraTouchDrag` Axis2D (per-finger slot 0 delta codes 1,2), `CameraTouchPan` Axis2D (two-finger centroid codes 4,5), and `CameraPinch` Axis1D (two-finger distance code 3). Existing `EnhancedInputAdapter` has produced these channels since Stage 17, and RTS now maps them explicitly like other input assets.

**RTS client:** `GameClient::event` keeps world-ground projection, touch selection/drag logic and camera distance clamping, but uses the Action values for single-finger pan, two-finger centroid pan and relative pinch zoom. `CameraPinch` modifiers can tune sensitivity without changing raw contact baseline. If a custom project's full `input/actions.json` lacks these actions, it falls back to legacy world-space touch deltas for compatibility.

**Tests authored (deferred):** `enhanced_rts_input_tests.cpp` expects five default RTS contexts and verifies TopDown context toggling, `RTS.Gestures` active. Existing platform adapter test covers raw two-finger motion. No CI/CTest run requested now.

**Next:** source audit, remove unnecessary duplicate legacy state where safe, document what project-owned touch HUD/Script abilities intentionally remain specialized. Do not move gesture projection or network Commands into the platform input library.

## Stage 33 — action-aware RTS pointer selection and commands (2026-11-18)

**Problem:** although RTS.Pointer had `Select`/`Command` Actions, the world input handler still used physical `MouseButton(0/1)` to start drag-selection or smart orders. An authored rebinding could update InputSystem without changing gameplay.

**Fix:** `ClientInputRouting::logicalMouseButton(physical)` resolves a native button through the currently active `RTS.Pointer` context, treating Select as logical 0 and Command as logical 1. Unassigned buttons return -1, middle mouse remains logical 2 for panning by default; ambiguous duplicate physical bindings are visible via `InputSystem::conflicts` and Select takes precedence. `GameClient::event` preserves the original native Event for UI/platform routing and before passing world clicks to script commands, tools, selection and smart orders rewrites only the logical button in a local Event copy. This makes RTS selection/command mouse-button rebinding functional **without** duplicating the geometry of click/box/garrison interactions, and bypasses it for top-down, planet and region modes.

**Tests authored (deferred):** RTS bridge test checks logical 0/1/2 defaults and rebinding Select to mouse-button 3 while suppressing unbound physical button 0.

**Next:** revisit top-down dynamic game.json abilities and input focus UI ownership, then cross-file source review. No tests run as requested.

## Stage 34 — top-down project-configured ability keys through Actions (2026-11-18)

**Problem:** top-down movement/weapons used `RTS.TopDown` Actions but custom `game.json.topDown.buttons[*].key` still bypassed Enhanced Input, so generated game-specific ability buttons could not be remapped through the shared API.

**Fix:** when a top-down hero is configured, `GameClient::topDownConfigure` synthesizes `TopDownAbility0..N` Boolean Actions and `RTS.TopDownAbilities` Mapping Context from the game's configured button keys if the project hasn't authored that context already. The context uses Pressed triggers and stable binding IDs. Project-authored `RTS.TopDownAbilities` can override this, preserving full user control. `ClientInputRouting::setTopDownEnabled` toggles the optional ability context together with `RTS.TopDown`. `topDownEvent` now dispatches buttonDown based on semantic Triggered Action names; per-key held state releases the corresponding button when its Action becomes inactive, even if the author changed its physical key. On focus loss all held ability flags and virtual controls are canceled.

**Compatibility:** touch HUD pointer buttons and the ability execution rules remain in `TopDownControls`/native Commands, not generic input. Game commerce scripts and vehicle authority remain specific mechanics. The generated context is runtime-only; for authoring custom controls, include `RTS.TopDownAbilities` and `TopDownAbilityN` Actions in the project's full input asset.

**Source audit fix:** corrected the `in::Device` typo in `enhanced_rts_input_tests.cpp` to the declared `ei::Device` namespace. Tests are authored but not executed per user request.

**Next:** finish source-level review, ensure no legacy direct key leaks, and leave runtime/CI/CTest to the subsequent validation phase.

## Stage 35 — frame-advancing RTS and top-down time triggers (2026-11-18)

**Bug:** `ClientInputRouting::onEvent` advanced the generic InputSystem with `dt=0` only on OS messages. Timed `Hold`, `Pulse`, `HoldAndRelease`, and continuous analog state would not advance in RTS during a stationary held key, even though FPS sampled input every frame.

**Fix:** `ClientInputRouting::advance(dt)` advances Action lifetimes once each presentation frame and returns semantic Triggered names. `GameClient::frame` invokes it after the existing gamepad injection step while playing and without a text field/modal how-to; otherwise it cancels held state. `GameClient::applyRtsActions` centralizes high-level RTS orders/FocusCamera for both physical event and timer-triggered events, producing the same existing Commands. Custom script-command hotkeys still win: `ClientInputRouting::suppressKey` releases the physical Action control on script override so delayed Hold/Pulse cannot later steal it. `topDownTimedActions` dispatches timed custom ability Actions into the existing module button state; `topDownFrame` releases held abilities when their Action becomes inactive, including canceled contexts.

**Files:** `apps/client/ClientInputRouting.h`, `GameClient.cpp/.h`, `ClientTopDown.cpp`, docs and this handoff. No changes to deterministic sim / ECS command formats.

**Validation deferred:** tests and gameplay smoke pass after implementation phase, per user. Verify script override, UI text capture, 0-dt event press, frame hold trigger, topdown button release and FPS parity later.

**Next:** close outstanding low-level editor input and cross-file audit; avoid treating per-event Action update as a substitute for per-frame updates.

## Stage 36 — editor direct GameWindow pointer binding parity (2026-11-18)

**Fix:** `apps/editor/windows/GameWindow.cpp` direct RTS play-test now resolves physical mouse buttons via the same authored `RTS.Pointer` context as the native RTS client, with shared semantics `Select` and `Command`. A focused helper `boundButton(action,legacyFallback)` finds the configured mouse button (or returns ImGuiMouseButton_COUNT when explicitly unbound); `actionMouseClicked/Down/Released` are used by selection boxes, building placement/cancel, script-card picking, region operations, and mouse-owned entity/action gestures. `refreshCameraActions(ed)` now runs **before** script-card click processing each frame so live project edits apply immediately to all world pointer paths. Camera middle-drag does not steal input if Select or Command has been explicitly rebound to middle.

**Compatibility:** pointers still go through ImGui/HUD capture and spatial picking in the editor; this is a presentation-layer mapping only, never deterministic ECS state. If a project's older full input asset has no `RTS.Pointer`, legacy left/right buttons remain the fallback. Custom bindings to non-mouse devices are not synthesized into fake editor mouse clicks; those need a project-defined UI interaction rather than reinterpreting cursor coordinates.

**Validation deferred:** source committed. During manual validation, test selected Game dock, script cards and viewport, rebind Select to button 3, drag boxes, Command to button 4, and middle-button camera pan collision. Recheck editor GameWindow input in hosted-client mode (separate event forwarding).

**Next:** release-stage static source audit, docs status, CI later; do not mark PR green without build evidence.

## Stage 37 — normalized two-finger twist input (2026-11-18)

**Adapter:** `EnhancedInputAdapter::updateGesture` now publishes `Device::Touch`, code 6, deviceIndex 0 for the shortest-arc two-finger rotation delta in radians (signed, wrapped to -π..π). It uses the same gesture activation/re-baseline guard as pinch/pan, so adding/removing a finger does not jump orientation. Camera consumers must integrate it per frame; the platform remains presentation-only.

**Preset:** `RTS.Gestures` adds `CameraTwist` Axis1D bound to Touch code 6, non-consuming. Existing codes 0..5 and mapping IDs are unchanged; only a new mapping is appended, so prior per-user override IDs remain stable.

**Test authored (deferred):** adapter test checks a deliberate rotated two-finger segment produces nonzero twist and does not lose pinch handling. No execution as requested.

**Next:** use normalized `CameraTwist` with planet orbit camera, and wire planet/possession movement to semantic Actions. Keep scripting and world projection genre-specific.

## Stage 38 — planet orbit and possessed-unit Action contexts (2026-11-18)

**Presets:** `RTS.Planet` (inactive except planet view) defines `PlanetRotate` Axis1D (Q/E), `PlanetPitch` Axis1D (PageUp/Down), `PlanetNextBody` Pressed (Tab). `RTS.Possession` (inactive except controlled unit) defines `PossessedMove` Axis2D (WASD/arrows), `PossessedFire` (left mouse) and `PossessedWalk` (right mouse). All previously published context IDs remain stable; default RTS now has **seven** contexts. `ClientInputRouting::setPlanetEnabled`/`setPossessionEnabled` maintain active contexts by game mode from both `GameClient::event` and `frame`.

**Possession:** `ClientActions.cpp::steerPossessed` computes camera-relative motion from semantic axis when available, otherwise legacy keys. Mouse fire/walk holds use semantic Action state with legacy fallback. The pointer logical LMB/RMB remapping path excludes possession to avoid stealing its gameplay controls.

**Planet:** `ClientPlanet.cpp::planetCameraKeys` uses shared `CameraPan`/ `IdleCameraPan` plus new `PlanetRotate/Pitch` and retains edge scroll/selection-dependent gating. `planetCameraEvent` takes semantic Triggered action names so rebinding Tab to `PlanetNextBody` and Space to FocusCamera works without repeat on unrelated keydown. Wheel zoom and one/two-finger pan/pinch/twist now consume `CameraZoom`, `CameraTouchDrag`, `CameraTouchPan`, `CameraPinch`, `CameraTwist` state, with legacy fallback for complete custom project assets lacking semantic actions.

**Tests authored (execution postponed):** RTS routing expectations updated to seven contexts; verify planet/possession activation and Axis2D motion. Follow-up smoke list: Q/E with selections, orbit body cycle, remapped wheel pinch/twist, possession fire/steer, reset on leaving view.

**Next:** audit contextual activation and C++ headers; finish docs/agent handoff. Then code is feature-complete for the stated scope, pending dedicated build/test/bugfix pass.

## Stage 39 — code-only integration audit / top-down project switches (2026-11-18)

**Source correction:** `GameClient` now tracks whether `RTS.TopDownAbilities` was synthesized by the engine. A later `topDownConfigure` re-registers an engine-generated ability mapping when the map/hero changes, preventing stale ability keys/indices from a previous level. A full authored `RTS.TopDownAbilities` context is never overwritten. Existing preset IDs remain unchanged.

**Editor correction:** `GameWindow` no longer refreshes project input twice per frame (once before script-card click processing, again in its input path); the earlier load remains, which is necessary so all direct-play pointer/camera actions use the same current authored scheme.

**Static audit (no test execution):** inspected source files for native core, adapter, JSON/profile file I/O, per-local-user routing/profiles, FPS, RTS, top-down, planet, editor, and CTest. Basic bracket/quote balancing across 21 files found no unmatched delimiters, and declared top-down/planet method signatures match their call sites by text inspection. This is NOT a compiler, linker, unit-test or runtime validation; platform-specific type correctness and behavior remain unverified at user's request.

**Post-implementation validation checklist:** (1) configure/build editor, RTS and FPS targets on macOS Apple Silicon, Linux, Windows; (2) CTest `library_enhanced_input`, `library_enhanced_input_adapter`, `library_enhanced_input_config`, `library_enhanced_rts_input`; (3) full ECS/determinism and FPS/RTS regression tests; (4) FPS F10 capture+Swap+restart+reset and project JSON reload; (5) editor dock focus/tab switching, project/Input window and direct GameWindow mouse selection; (6) keyboard/gamepad, drag, gestures and top-down/planet/possession mode transitions; (7) local multiplayer owner swap and per-player profiles; (8) Android/iOS/Web input smoke tests. Fix test/build failures in this same feature branch and append Stage 40 logs before making PR ready.

**Feature status:** implementation delivered at source level, tests intentionally deferred. PR #20 stays Draft; no claim of green CI or working binary.

## Stage 40 — synchronize latest inspector and handoff implementation to test phase (2026-11-18)

**Upstream:** merged `feature/universal-entity-inspector-fps` commit `309d7035e7317c5e3be66e8118298d2b64d490a1` (docs-only unified test runner + editor-unification handoff changes) as an actual two-parent merge into the input feature branch. No code or tests were overwritten, and input branch now contains its inspector parent history.

**Code implementation handoff:** the requested Enhanced Input functionality is committed: typed Actions and contexts, Modifiers/Triggers including complex timing, priorities/consumption, optional same-control conflicts and rebind policies, keyboard/mouse/touch (pan/pinch/twist)/pad, per-local-player device routing, per-user validated/transactional profiles and reset, FPS F10 Settings and project schemes, RTS keyboard and command bindings, top-down movement/weapons/abilities, possessed-unit actions, planet camera and gestures, editor Input Actions authoring and direct/hosted viewport capture. All code changes are presentation/native only; game rules continue using existing ECS systems and validated Commands.

**Test-phase entry gate:** the user explicitly asked to **finish code first and run tests afterward**. Therefore no compile, link, CTest, native GUI, device smoke, CI-success assertion, merge approval or release is claimed. PR #20 remains a DRAFT until build/test failures are resolved and evidence is recorded as Stage 41.

**Commands for the next agent:** run from repository root `cmake -S tests/libraries -B build/enhanced-input -DCMAKE_BUILD_TYPE=Debug`, then `cmake --build build/enhanced-input --parallel 2 --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests`, then `ctest --test-dir build/enhanced-input --output-on-failure -R "^library_enhanced_(input|rts_input)"`. Run full `./run-tests.sh` per AGENTS.md and platform/editor client smoke checks. Use the existing focused GitHub workflow for Linux/macOS/Windows, then review full CI. Amend code/hand-off for each failure; only then mark PR ready.

**Watch-outs for testing:** static C++ inspection cannot validate touch reset timing, cross-platform path rename, ImGui version, simulated gamepad behavior, button overlap with custom scripts or split-screen transitions. The new multi-user API is a reusable integration building block, not a claim that the sample FPS game has complete split-screen rendering/network identity.

## Stage 41 — C++ NotChord source-scope fix (2026-11-18)

**Goal/status:** IMPLEMENTED source-level correction; COMPILATION/TESTS NOT RUN at user's request (implementation first).

**Defect:** `InputSystem::chordSatisfied` was missing braces around its `for (const auto& trigger : triggers)` loop. `NotChord` subsequently used `trigger` outside its C++ scope, causing a compile error. The previous source-level bracket-count audit did not catch this semantic scoping error.

**Changed files:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` (wrap both Chord and NotChord checks in the same loop); `docs/enhanced-input-agent-handoff.md` (this entry); `docs/enhanced-input.md` (correctness note). Public API and JSON format unchanged. Behavior intended: every Chord/NotChord constraint in the trigger span is evaluated against the same-frame action-level map; the first unsatisfied constraint rejects the mapping.

**Verification:** static source inspection only. `library_enhanced_input` and the full CI have NOT RUN / no passing run evidence. The existing `tests/libraries/enhanced_input_tests.cpp` advanced trigger scenarios already include `Trigger::notChord`; rerun those when testing is authorized.

**Known risks:** composite trigger edge cases and time advancement still warrant deeper audit. A separate thread is pushing commits to `feature/universal-entity-inspector-fps`; compare and merge upstream before final PR review.

**Next agent task:** inspect `EnhancedInput.h::update/evaluateTriggers` for trigger lifetime correctness, especially same-frame press/release, DoubleTap and gated chords. Update regression cases, then append Stage 42 before advancing. Do not claim compilation success or merge PR #20 yet.

## Stage 42 — context-priority and modifier-aware chord preview (2026-11-18)

**Goal/status:** IMPLEMENTED source correction. Build/tests deliberately **NOT RUN** (user asked to finish implementation before testing).

**Defect:** `InputSystem::update` previously built same-frame `rawLevels` for Chord/NotChord from *all* physically actuated controls without checking active-context consumption, action-level modifiers or ValueType constraints. This could incorrectly fire `AltFire` on a gameplay Aim button already consumed by a higher-priority Menu context, or on Aim suppressed by an Action DeadZone.

**Change / API:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` now previews candidate action values in normal priority/name/registration order, honors consuming mappings, applies mapping and action modifiers and the target ValueType, and only then produces the chord-presence map. The preview is a read-only level calculation: it does not touch runtime timers/phases or emit actions. Public API and stored JSON schema are unchanged.

**Tests authored, not executed:** `tests/libraries/enhanced_input_tests.cpp` now asserts that a Menu context consuming Aim prevents a same-frame gameplay chord, and that Aim below an Action-level DeadZone threshold doesn't activate AltFire while Aim above the threshold does. Run `library_enhanced_input` during the later validation pass.

**Known limitation:** the preview establishes *physical/action-value presence*, not a full recursively evaluated trigger dependency graph. Chained Chord/NotChord gates and cycles still require an explicit resolution policy; do not describe this as recursive Unreal dependency parity.

**Next agent task:** inspect `EnhancedInput.h::chordSatisfied/evaluateTriggers`, `EnhancedInputConfig.h::checkChords`, and `tests/libraries/enhanced_input_tests.cpp`. Define deterministic handling of cyclic and chained chord dependencies without evaluation-order ambiguity, update API/config/editor docs and tests, and write Stage 43 before proceeding. Keep legacy `Input.h` and simulation Commands unchanged.

## Stage 43 — introduce AIVerse InputFlow as the official subsystem name (2026-11-18)

**Goal/status:** IMPLEMENTED the canonical public C++ name in GitHub; compile/CTest and platform validation **NOT RUN** (user requested implementation before testing). Product name: **AIVerse InputFlow** (short: **InputFlow**), emphasizing the pipeline of Device → Mapping Context → Action → Trigger → gameplay Commands, independently of game genre.

**Changed files/API:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` renames the C++ implementation class from `InputSystem` to `InputFlow` and adds `using InputSystem = InputFlow` in the legacy `aiverse::lib::gameplay::enhanced` namespace. New canonical public header `libraries/include/aiverselib/gameplay/InputFlow.h` exports `aiverse::lib::gameplay::inputflow::InputFlow`, associated Actions, Contexts, Values, Modifiers, Triggers, Controls and RebindPolicy; offers the friendly aliases `InputAction`, `InputContext`, `InputValue`, `InputActionEvent`, and `InputActionState`. New code should include `InputFlow.h`, not depend on the old implementation header directly. Existing `EnhancedInput.h` consumers, `enhanced::InputSystem`, JSON schema v1 (`input/actions.json`) and `AIVERSE::lib_input` continue to work unchanged.

**Documentation:** `docs/enhanced-input.md`, `skills/ui/aiverse-input/SKILL.md`, and root `AGENTS.md` now identify AIVerse InputFlow as the canonical public API. Legacy file names/IDs are intentionally preserved for source and asset compatibility.

**Evidence:** GitHub source commits: `8ccca5eb83ba82897599dd9711872016e4b1a6ab` (renamed implementation and alias), `b4b7b3ec5ddb3f8637c3252237e640b1252608f4` (new facade). No tests run, no CI pass claimed.

**Known risks/limitations:** Tooling, editor window, FPS/RTS clients and platform adapters still need to adopt `inputflow::InputFlow` and visible InputFlow labels; old API remains supported. The class-name change should be compile-verified in the later test phase.

**Next agent / Stage 44:** Update `engine/platform/EnhancedInputAdapter.h`, `EnhancedInputConfig.h`, `EnhancedInputPresets.h`, `EnhancedInputUsers.h`, `EnhancedInputUserProfiles.h`, `apps/fps/client.cpp`, `apps/client/ClientInputRouting.h`, `apps/editor/windows/InputWindow.cpp` and `GameWindow.cpp` to include `aiverselib/gameplay/InputFlow.h` and use the `inputflow` namespace/branding; retain compatibility paths, stable IDs and JSON schema. Update tests, catalog, and this handoff after that stage. Testing remains deferred.


## Stage 44 — apply InputFlow name throughout runtime, editor, tests and catalog (2026-11-18)

**Goal/status:** IMPLEMENTED IN SOURCE; compilation, CTest and device testing **NOT RUN** per the user's implementation-first instruction.

**Canonical C++ API:** `libraries/include/aiverselib/gameplay/InputFlow.h` exports `aiverse::lib::gameplay::inputflow::InputFlow` and the related `InputAction`, `InputContext`, `InputValue`, `InputActionEvent`, and `InputActionState` aliases. New `engine/platform/InputFlow.h` is the platform facade: `InputFlowAdapter`, `InputFlowUsers`, `InputFlowUserProfiles`, `InputFlowAsset`, `InputFlowProfile`, `inputflow_config`, `inputflow_presets`. Underlying engine logic remains presentation-side; no authoritative ECS or command changes.

**Exact files changed:** `libraries/include/aiverselib/gameplay/{EnhancedInput.h,InputFlow.h}`; `engine/platform/{InputFlow.h,EnhancedInputAdapter.h,EnhancedInputConfig.h,EnhancedInputPresets.h,EnhancedInputUsers.h,EnhancedInputUserProfiles.h}`; `apps/fps/client.cpp`; `apps/client/ClientInputRouting.h`; `apps/editor/windows/{InputWindow.cpp,GameWindow.cpp}`; `tests/libraries/{enhanced_input_tests.cpp,enhanced_input_adapter_tests.cpp,enhanced_input_config_tests.cpp,enhanced_rts_input_tests.cpp}`; `libraries/catalog.json`; `.github/workflows/enhanced-input.yml`; `AGENTS.md`; `docs/enhanced-input.md`; `skills/ui/aiverse-input/SKILL.md`; this handoff. UI names are now **InputFlow Settings (F10)** and **InputFlow Actions**; catalog's canonical source and CI display name use InputFlow. Core tests include a compile-time alias compatibility assertion.

**Compatibility invariants:** Keep `EnhancedInput.h`, `enhanced::InputSystem` (alias of the InputFlow implementation), the `EnhancedInput*` platform headers and their old classes, `AIVERSE::lib_input`, `input/actions.json`, user profile filenames and CTest target names. They are legacy identifiers, not a different implementation. All projects with the old API remain intended to work without migration.

**Actual verification:** Source changes accepted by GitHub; no local compiler run, no CTest, no real device checks, no passing CI results verified. Status **NOT RUN**, not PASS. Automated checks may still report queued or cancelled because of incremental commits. Merge PR #20 only after validation in the dedicated later phase.

**Known risks:** C++ namespace/class alias usage in all platform targets and ImGui UI labels require compile/runtime verification. Some old filenames and docs deliberately retain EnhancedInput spelling. Version 1 authored assets and saved profiles must not be renamed by the branding change.

**Next agent (Stage 45):** Read both InputFlow.h headers, then `apps/fps/client.cpp`, `apps/client/ClientInputRouting.h`, `apps/editor/windows/InputWindow.cpp`, `libraries/catalog.json` and this handoff. Continue any feature work with canonical InputFlow names; after each stage append a fresh numbered handoff. When the user authorizes the test phase, compile the four existing `aiverse_enhanced_*_tests` targets and full editor/FPS/RTS builds, then run `./run-tests.sh` and review cross-platform CI. Fix failures and record command, SHA, PASS/FAIL in Stage 45 before marking Draft PR ready.


## Stage 45 — canonical InputFlowAdapter/InputFlowUsers implementation names (2026-11-18)

**Status:** IMPLEMENTED at source level. No build, CTest, macOS GUI or other platform tests have been run; **NOT RUN** by user request.

**Goal/change:** Complete the InputFlow naming migration in the implementation rather than relying solely on facade aliases. `engine/platform/EnhancedInputAdapter.h` now declares the concrete C++ `class InputFlowAdapter` and its constructor under that name; declares `using EnhancedInputAdapter = InputFlowAdapter` for existing code. `engine/platform/EnhancedInputUsers.h` similarly declares concrete `class InputFlowUsers`, uses `InputFlowAdapter` to host each user's independent context, and retains `using EnhancedInputUsers = InputFlowUsers`. `engine/platform/InputFlow.h` imports those concrete classes rather than introducing duplicate aliases. `tests/libraries/enhanced_input_adapter_tests.cpp` has compile-time `static_assert` checks for both old/new type identities.

**Documentation updated:** `docs/enhanced-input.md` documents the concrete class names and keeps the example scoped within a compilable C++ function. `skills/ui/aiverse-input/SKILL.md` clarifies that InputFlow is canonical for new native project code.

**Compatibility:** Old source includes, old adapter/user class names, project JSON v1, per-user profile files, `AIVERSE::lib_input`, FPS/RTS native Commands and ECS behavior remain unchanged. Old header filenames are intentionally retained as stable includes. Public canonical headers: `libraries/include/aiverselib/gameplay/InputFlow.h` and `engine/platform/InputFlow.h`.

**Verification:** GitHub accepted changes. No compiler run and no passing CI/CTest result is claimed. Test phase should verify API aliases on GCC, Clang and MSVC, then validate editor, client and gamepad lifecycle behavior.

**Next agent:** Check the actual HEAD commit before making changes, read this handoff and root `AGENTS.md`; continue feature work using canonical InputFlow names. Once the user authorizes tests, build the four existing Enhanced Input targets and use the unified `./run-tests.sh` runner, record exact test SHA, errors and corrections as Stage 46 before marking PR #20 ready.

## Stage 46 — canonical InputFlow CMake target in engine and tests (2026-11-18)

**Status:** IMPLEMENTED. Compile/CTest/macOS GUI/cross-platform checks **NOT RUN**; the user requested all implementation first, testing afterward.

**Goal:** Finish the code-level naming migration to **AIVerse InputFlow**. Stages 43–45 previously introduced the canonical public headers `libraries/include/aiverselib/gameplay/InputFlow.h` and `engine/platform/InputFlow.h`, renamed the concrete types to `InputFlow`, `InputFlowAdapter`, `InputFlowUsers`, and migrated FPS/RTS/editor implementation references while retaining old headers and type aliases.

**Changes in this stage:**
- `libraries/CMakeLists.txt`: `add_library(AIVERSE::InputFlow ALIAS aiverse_lib_input)` and `add_library(AIVERSE::lib_inputflow ALIAS aiverse_lib_input)`. Both use the **same** generated C++20 header-only package as `AIVERSE::lib_input`; there is no duplicate input runtime.
- Root `CMakeLists.txt`: game client, mobile/web client and editor link `AIVERSE::InputFlow` instead of the legacy library name.
- `tests/libraries/CMakeLists.txt`: existing input core, adapter and RTS regression executables link `AIVERSE::InputFlow`, keeping their stable target/test names.
- `libraries/catalog.json`, `skills/ui/aiverse-input/SKILL.md` and `docs/enhanced-input.md`: document canonical link names and old name compatibility.
- This stage is recorded in `docs/enhanced-input-agent-handoff.md`.

**API/compatibility:** New native code: `#include <aiverselib/gameplay/InputFlow.h>`, `aiverse::lib::gameplay::inputflow::InputFlow`, `platform::InputFlowAdapter`, CMake `AIVERSE::InputFlow`. Older `EnhancedInput.h`, `enhanced::InputSystem`, `AIVERSE::lib_input`, JSON `input/actions.json`, saved profiles and serialized gameplay Commands remain supported. The old EnhancedInput file names are retained as include compatibility shims.

**Verification:** GitHub accepted the commits; no passing compilation or runtime test has been observed. This CMake alias must be exercised in the later verification pass. The current PR remains a Draft.

**Next-agent task:** Read the latest `AGENTS.md` and this handoff; inspect `libraries/CMakeLists.txt` alias ordering, `CMakeLists.txt` input consumers and `skills/ui/aiverse-input/SKILL.md` for the current code API. Preserve the alias target and the old API. When the user authorizes testing, run the four existing input tests via `./run-tests.sh`, build FPS, RTS and editor targets, then record exact Git SHA, commands and PASS/FAIL in a new Stage 47 before changing PR readiness.


## Stage 47 — transitive Chord/NotChord and cycle rejection (2026-11-18)

**Goal/status:** IMPLEMENTED IN GITHUB SOURCE. Compiler/CTest, real FPS/RTS/editor play, gamepad/touch and CI **NOT RUN** (per user direction: finish implementation first, run tests later). This stage closes the explicit Stage 42 dependency-graph follow-up; PR #20 must stay DRAFT.

**Problem:** The prior InputFlow preview used physical/action-modified levels but ignored a *referenced action's own chord gates*. A -> B -> C chain could activate A even if B required C and C was not active. A positive/negative cycle was not explicitly rejected, so nontrivial NotChord graphs could have ill-defined behavior.

**Architecture/API:**
- `libraries/include/aiverselib/gameplay/EnhancedInput.h`: private `detail::chordGraphAcyclic` checks both Action and Mapping trigger dependencies (Chord and NotChord), including cycles crossing contexts. `InputFlow::defineAction` and `registerContext` validate a staged graph before changing a live definition; a rejected change does not reset existing action/mapping runtime state.
- `InputFlow::update`: preserves the context-priority, consumption- and modifier-aware preview from Stage 42, records candidate mapping contributions, then resolves **effective** Action chord levels recursively before the normal mapping/action trigger dispatch. Mapping gates, Action gates, Action modifiers and ValueType are included; temporal triggers still govern events and phases rather than the prerequisite's held level. Memoization makes the result independent of lexical Action order. A defensive recursion guard returns false on a malformed live cycle, but authored/native cycles should be rejected before update.
- `engine/platform/EnhancedInputConfig.h::readAsset`: validates the full graph after ordinary missing-action/self-chord checks, rejects cycles with the explicit error `input: cyclic chord/notChord dependency`, leaves the caller's Asset unchanged.
- No change to `input/actions.json` v1 schema, stable mapping IDs, per-user profiles, old Enhanced Input API, serialized FPS/RTS Commands, deterministic ECS or engine/platform device events.

**Exact source changes / Git commits:** core `226c736fdd78c8d95a5181ba78774cf5a31b2b4e`; config `dd2b4e60b34aa5c4695992817b9c9f47030f4911` and namespace correction `f6fbec3fff732d7a9a932b5962f905ded84ebdc8`; authored core regression `9951eee05c1a652bacc15c4f6a8d6a42350c3fe0`; authored JSON regression `e36f42a694290993ead37378a24606cf2d9bd430`; user guide `5c7eec09ac6e96fb98d6b992a70a0614f9a66739`; agent skill `56dee96b0d6d3bb16686d026b28de47c70160fee`. The current handoff commit follows these.

**Tests AUTHORED, NOT EXECUTED:** `tests/libraries/enhanced_input_tests.cpp` now checks a Stance -> Shift -> Finisher chain, absent Shift suppressing the Finisher, present Shift enabling a same-frame trigger, and native positive/negative cycle rejection without context mutation. `tests/libraries/enhanced_input_config_tests.cpp` verifies that an Action A -> B -> A Chord/NotChord cycle is rejected transactionally and returns a cycle error. Existing context-priority and Action deadzone chord tests are retained. **No PASS or CI-green assertion**.

**Validation commands for later test phase:** `cmake -S tests/libraries -B build/enhanced-input -DCMAKE_BUILD_TYPE=Debug`; `cmake --build build/enhanced-input --parallel 2 --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests`; `ctest --test-dir build/enhanced-input --output-on-failure -R 'library_enhanced_input'`; then native editor/FPS/RTS builds and `./run-tests.sh`. Record exact SHA/command/result only when actually run.

**Known limitation/risk:** The prepass reserves an actuated consuming physical control *before* evaluating the mapping's recursive chord gate. A conditionally gated high-priority mapping that shares one control with a lower-priority mapping can shadow the latter even if its own chord is false. Explicit `consume=false` avoids the conflict. Mixed Action-level deadzones and multiple-gated mappings need real compile and behavior testing; do not mark this edge case resolved. Native C++ library compile across Clang/GCC/MSVC is also unverified.

**Next agent task (Stage 48):** Inspect `InputFlow::update` Pass 1/1b vs Pass 2 in `EnhancedInput.h` to unify effective-gate-aware control consumption without introducing same-frame order dependence. Add tests where a higher context consumes one control only if its Chord prerequisites are satisfied, while lower mapping can otherwise receive it. Preserve Cycle rejection, action modifiers, stable IDs and compatibility aliases. Update `docs/enhanced-input.md`, `skills/ui/aiverse-input/SKILL.md`, and append Stage 48 handoff with source/actual test evidence. Keep the implementation-first/test-later instruction unless the user explicitly changes it.


## Stage 48 — gate-aware context consumption and chord preview reconciliation (2026-11-18)

**Status:** IMPLEMENTED IN GITHUB SOURCE, tests authored. Native compiler/CTest, GUI, platform smoke and CI **NOT RUN**; preserve user's implementation-first, test-later request. Remain on `feature/aiv990_enhanced_input`, PR #20 Draft.

**Defect:** Stage 47 recursively resolved chord providers but its preliminary preview reserved a consumed control as soon as a high-priority mapping was physically actuated, even when its Mapping-level Chord was false. This made a lower-priority Action disappear from the *chord preview*, and a downstream action requiring that lower Action would incorrectly fail in the same frame.

**Implementation/API:** `libraries/include/aiverselib/gameplay/EnhancedInput.h::InputFlow::update` now has `rebuildCandidates(levels)`, which conditionally reserves a physical Control only when the Mapping's Chord/NotChord gates pass against the prior effective Action levels. `resolveLevels()` reuses the Stage 47 recursive resolver. If active Mapping-level chords exist, a bounded refinement (up to 8 rounds) alternates the two until the Action-level map stabilizes. A repeated/nonconvergent map restores the conservative raw-priority candidate snapshot rather than depending on arbitrary iteration counts. No public API, device backend, mapping IDs, asset/profile schema or deterministic ECS Commands changed. Stage 48 covers **Mapping-level** gating; Action-level timing triggers still control events, not whether the Mapping reserves a physical Control.

**Changes/commits:** `EnhancedInput.h` gate-aware candidates `7ba61a158abc4d720936d485cfefcbc7f98e53d7`, explicit branch structure `2ea7bb20d903651191b1b0f0475ae7bbdc2c0373`; `tests/libraries/enhanced_input_tests.cpp` `e29a7ce4cf3505dba0b09111b0b52213d5e185e5`; `docs/enhanced-input.md` `59277ea3e1842c653c59ff9eac4fc84b4c3c2f48`; `skills/ui/aiverse-input/SKILL.md` `100d74ff5846e8490d6caf72b95b9dff52b16f36`; this handoff follows.

**Test authored (NOT RUN):** `enhanced_input_tests.cpp` creates a higher-priority UI mapping consuming keyboard key 131 only with `Chord(Modal)`, a lower Game mapping using key 131 as Move and a FollowUp action requiring Move. Checks (a) Modal absent: Move active and FollowUp fires on its own key; (b) Modal present: UI mapping active, Move blocked, FollowUp suppressed. The prior transitive chain, inversion, native/JSON cycle rejection, modifiers and context-priority assertions remain. No PASS assertion is warranted before actual build/CTest.

**Known limitations/risks:** A Chord dependency that depends on a lower-priority mapping of the *same* control being consumed by the higher mapping may make priority/Chord refinement non-monotonic. The bounded algorithm detects repeated states or the iteration cap and restores the conservative prepass; it does **not** promise activation of mutually unsatisfiable consumption feedback. `consume=false` is the supported intentional-sharing option. Performance on hundreds of active conditional mappings and compiler portability are unmeasured. A disabled Mapping after a failed gate must not leave a stale held state; test focus, context switches, and repeated key presses later.

**Next-agent task — Stage 49:** Source-audit `InputFlow::update` for runtime edge transitions when Mapping Chord availability changes *while the physical control remains held*, and when a high-priority context activates/deactivates during Hold/Pulse. Add focused tests proving no phantom Pressed, duplicate hold, or stuck Action; keep the current gameplay Command layout and all legacy aliases. Append a Stage 49 handoff after edits, with exact files, commits, actual tests (NOT RUN unless explicitly requested). At validation time run the four InputFlow unit binaries, `ctest --test-dir build/enhanced-input --output-on-failure -R 'library_enhanced_input'`, full editor/FPS/RTS builds and `./run-tests.sh`.


## Stage 49 — Action/Mapping lifetime on chord and context interruption (2026-11-18)

**Status:** IMPLEMENTED IN GITHUB SOURCE, regression tests AUTHORED ONLY; compiler, CTest, IDE, GUI and device tests NOT RUN by prior user instruction. Continue on `feature/aiv990_enhanced_input`; PR #20 stays Draft.

**Bug:** Mapping and Action trigger runtime could keep Hold/Pulse/DoubleTap state when a Chord/NotChord failed or a higher context consumed its control. A raw held control could still report an Action active when the Action-level chord gate failed, and returning to a context could synthesize a new Pressed from a physically held control. Switched contexts generally emitted Completed instead of canceled, confusing one-shot/held gameplay.

**Implementation:**
- `libraries/include/aiverselib/gameplay/EnhancedInput.h`: `InputFlow::update` makes effective Mapping contributions empty when blocked or chord-ineligible; `resetTiming(Runtime&)` discards elapsed Hold/Pulse and DoubleTap release histories on ineligibility. Mapping `runtime.wasDown` tracks the original physical level during suppression so regaining a chord does not automatically count as a new physical Pressed; Hold starts accumulating from the first eligible frame.
- Action-level chord gating now affects `ActionState.active` and `value` (zero if gated out), not only `Triggered`. A gate-loss or context-consumption interruption emits `Canceled` when an Action was active. Normal releases keep prior Completed/Canceled semantics.
- `activateContext`, `registerContext` (active replacement), and `rebind` seed active Mapping history from persistent held control samples to reduce spurious Pressed on context re-entry. New `presentedActions` tracks disabled context suppression.
- No change to any FPS/RTS Commands, deterministic sim/ECS, input/actions.json version, profile IDs or old InputSystem compatibility API.

**Files/commits:** core `869801de57cac2e6f6057371ebb2229319f0f304` and cancel correction `73ed1c019751c77278aed4d8d6d4fa63198bf741`; tests `fbe2893e0bbe4ac73de0ed471831b27b5c2296f2` and modal expectation `8693074a94acdc283e9c9db421498093e9794693`; user doc `4c7e4bf57d1003d7eda7d67ed16250bcb3f4d526`; agent skill `a055d670ca452446f2ef96ea338f0ab5538325de`; this handoff follows.

**Tests AUTHORED / NOT EXECUTED:** `tests/libraries/enhanced_input_tests.cpp` covers Hold re-arming after loss/regain of a Mapping chord while key remains held, Action-level chord active/cancel semantics, and no duplicate Pressed when reactivating a context over held control; existing modal-shadow test expects Canceled. Exact commands for later test phase: `cmake -S tests/libraries -B build/enhanced-input -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build/enhanced-input --parallel 2 --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests`, `ctest --test-dir build/enhanced-input --output-on-failure -R 'library_enhanced_input'`; also full editor/FPS/RTS builds and `./run-tests.sh`.

**Known limits/risks:** When a context is activated *after* an input edge arrives but before its first `update`, a fresh raw `pressed` flag may still be observed. GUI/host input ordering needs manual verification. New context recovery uses the same physical held value for `Down` (intentional), but old projects with `Pressed` after context switching should be exercised. Engine-specific device reassignment and editor focus are separate from the core change; no build/test success is claimed.

**Next agent — Stage 50:** review `engine/platform/EnhancedInputUsers.h` and `EnhancedInputAdapter.h` for multi-local-user device ownership, especially reassigning keyboard/pointer/gamepad without clearing unrelated device state, repeated focus loss, pointer capture and per-player gamepad index conversion. Add regression cases to `enhanced_input_adapter_tests.cpp`. Keep one physical backend poll per frame and no authoritative ECS direct access. Document stage and actual PASS/FAIL/NOT RUN.


## Stage 50 — device-scoped input cancellation for multiple local users (2026-11-18)

**Status:** IMPLEMENTED IN GITHUB SOURCE, tests authored only; no compiler, CTest, FPS/RTS/editor smoke, CI or physical devices have been run. Stay in `feature/aiv990_enhanced_input`; PR #20 Draft until validated.

**Problem:** Existing `InputFlowUsers::moveOwner`, `assignGamepad`, `unassignGamepad`, and `onEvent(e,false)` called `InputFlowAdapter::focusLost()`. Transferring a mouse between players could reset WASD, gamepad held actions and touch of the same owner, despite those devices remaining assigned. UI capturing a mouse event could incorrectly cancel keyboard/gamepad commands.

**Architecture/code:**
- `libraries/include/aiverselib/gameplay/EnhancedInput.h`: public `InputFlow::cancelDevices(std::initializer_list<Device>, int deviceIndex=-1)`. Removes matching raw control state, clears only affected Mapping runtimes and records affected Actions for `Canceled` at the *next* update if no other live source remains. Unlike global `cancelAll()` this doesn't erase unaffected controls, and it doesn't immediately emit events. Explicit `#include <initializer_list>`.
- `engine/platform/EnhancedInputAdapter.h`: `InputFlowAdapter::cancelKeyboard/cancelPointer/cancelTouch`; touch cancellation also resets finger ownership and gesture baseline. Global `focusLost()` remains full cancellation.
- `engine/platform/EnhancedInputUsers.h`: keyboard/pointer/touch owners use selective cancellation on transfer; unassign APIs `clearKeyboardOwner/clearPointerOwner/clearTouchOwner`; pad reassign/unassign cancels logical GamepadButton+GamepadAxis deviceIndex=0 only, preserving keyboard/mouse. `onEvent(e,false)` cancels just the event's class. `pollGamepads()` still uses one physical backend poll and stable logical pad index=0; per-device routing does not touch deterministic sim Commands.
- No changes to InputFlow asset/schema or user profile formats; backward-compatible class aliases remain.

**Source commits:** `5803cd2f725d0550f6473ff7c0ea91f8eea892a8` (core cancelDevices); `dcb81b57e33d0bfed15d7fa3e022d877bd82b6ee` (adapter); `eb5b36d91686938de65430e23a5da8ef49cebb13` (Users); `e5c8ce6b6c4d773f2e2450050d5593f76c572b12` (adapter tests); `4ae35359e7c3d570937b5338b8948b5b70876690` (docs); `df7134c13e88c995b2e61382bd8d98c232ec6ac1` (skill); this handoff follows.

**Authored tests — NOT RUN:** `tests/libraries/enhanced_input_adapter_tests.cpp` changes the earlier unassign test to await the next update and assert a Canceled phase; adds a user with simultaneous held keyboard Walk, pointer Click and controller PadJump; transfers pointer then keyboard and verifies the other devices remain active, then unassigns pad and verifies only PadJump is canceled. Also checks `onEvent(mouse,false)` preserves held keyboard and `clearKeyboardOwner` reports unassigned/cancels at update. Prior touch/gamepad tests remain.

**Risks/limits:** Device cancellation is deferred until `update(dt)`; a caller reading ActionState immediately after ownership transfer (without updating) sees the previous presentation-frame state. This is documented and intentional to preserve event lifetime; games must update before converting Actions to Commands. Nondefault gamepad deviceIndex mappings need direct single-player adapter or explicit local profile mapping; multi-user router normalizes assigned physical pads to logical index 0. Verify GUI modal capture, focus and hotplug across native platforms in later validation.

**Next agent — Stage 51:** audit FPS/RTS/editor focus ownership call sites and project asset fallback to ensure no stale raw key routing bypasses InputFlow, and investigate `EnhancedInputAdapter::updateGesture` terminal touch events for clean delta handling. Author tests before test-phase run. Keep input presentation separate from native Commands and ECS simulation. Update docs/skill/handoff at the end of each additional stage.


## Stage 51 — suppress phantom edges from already-held controls (2026-11-18)

**Status:** IMPLEMENTED IN GITHUB SOURCE and regression test AUTHORED; compiler/CTest/device smoke/CI NOT RUN by user request. Remain on `feature/aiv990_enhanced_input`, Draft PR #20.

**Problem:** Stage 49 seeded a new context's Mapping `runtime.wasDown` from held persistent controls, but the same frame's raw `pressed` flag could still trigger a new `Pressed`. Action-level `Pressed/Hold` could also treat a physically old hold as a fresh event, even when Mapping triggers were suppressed. Context activation just after an OS KeyDown could accidentally accept the button that opened the UI, fire an ability or age a Hold in a newly activated mode.

**Implementation:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` adds `Runtime::suppressUntilRelease`. `seedHeldMappings` sets that flag for held Mapping controls and, when an Action wasn't already active, seeds the Action runtime. While suppression is active, Mapping and Action time/edge triggers do not fire or accumulate (Pressed/Released/Tap/DoubleTap/Hold/HoldAndRelease/Pulse); continuous Down or pure Chord/NotChord predicates and triggerless bindings remain observable. The flag clears on a genuine physical release, after which a new KeyDown can produce the proper first edge. This preserves normal continuous movement on context return, while preventing ghost one-shots.

**Exact commits/files:** core `206fb356c5d06ac5e9b82ce14a96ff22e870d0d3`; `tests/libraries/enhanced_input_tests.cpp` `2fb1435fbd9b0d3af6110ba3419912e6c444f962`; `docs/enhanced-input.md` `4d049ae9241e42d722e88c99b76416ec3e3212de`; `skills/ui/aiverse-input/SKILL.md` `a155902bcc80ab1f4641b506f2ca53fe0f0a667c`; this handoff follows. No gameplay Commands, ECS sim contracts, profile names or JSON schema changed.

**Tests AUTHORED, NOT RUN:** core unit test buffers four KeyDown inputs before context activation, covering Mapping Pressed, Action Pressed, Action Hold and continuous Down. On immediate update, only Down should fire; on physical release then fresh key press, the one-shots and Hold should behave normally. Existing modal reactivation, chord timer and multi-user tests remain. Test phase commands from Stage 50 still apply.

**Known risks/limits:** An Action mapped to several held devices with different context lifetimes uses shared Action runtime; the seed intentionally skips suppressing an Action that was already active from another mapping, but a multi-device context switch may require GUI/device smoke. Pre-activation event suppression applies only to persistent levels (not relative mouse/touch deltas). No actual compile or behavior verification done.

**Next agent — Stage 52:** inspect the input integration across FPS/RTS/editor host for bypasses, review bounded conditional-chord refinement and rebind/profile persistence against source code, then write a release-readiness checklist documenting any unverified paths. Do not mark Draft PR #20 ready without the separately requested build/CTest/device/CI phase. Append Stage 52 to this handoff and synchronize docs/agent skill.


## Stage 52 — RTS UI capture isolation and legacy key-state correctness (2026-11-18)

**Status:** IMPLEMENTED IN SOURCE, tests AUTHORED, build/CTest/GUI/CI NOT RUN. Continue `feature/aiv990_enhanced_input`; PR #20 Draft.

**Defects / source audit:** `apps/client/ClientInputRouting.h::onEvent` previously called `adapter_.focusLost()` for any `!gameplayAllowed` event. Thus one RmlUi-captured mouse move destroyed held keyboard camera and unrelated touch/pad actions. `apps/client/GameClient.cpp::event` also unconditionally mirrored raw KeyDown to its legacy `keys_` array, even for keys consumed by UI/text/guide, allowing old top-down/possession fallbacks to observe captured keys. While a modal remained open no KeyUp was guaranteed to reach the game and stale flags could persist.

**Changed architecture/code:** `ClientInputRouting::onEvent` reserves global `adapter_.focusLost()` for OS FocusLost, but for captured device events now uses `cancelKeyboard()`, `cancelPointer()`, or `cancelTouch()`, followed by `input_.update(0.0f)` without emitting gameplay shortcuts. `GameClient::event` updates legacy `keys_` only for gameplay-eligible KeyDown, always clears on KeyUp, guards the Key::Count bounds; `GameClient::frame` clears that mirror whenever the keyboard is disabled by screen/modal/text ownership. Authored semantic Action flows and existing Commands/ECS data remain unchanged.

**Files/commits:** `apps/client/ClientInputRouting.h` `a077505e827b1bbd417ccdfb96c08ec9b3054a7c`; `apps/client/GameClient.cpp` `22d9ff8896522b59b87d09150c40d9f235db2a02`; `tests/libraries/enhanced_rts_input_tests.cpp` `fe6db85e54dd9265594e43ec6941dc5356af9d5d`; `docs/enhanced-input.md` `d58f150f43c1046973b0e032c1fd8240cdce0826`; `skills/ui/aiverse-input/SKILL.md` `a3e132162af30405b9d3e38a7a910cbde4b121c6`. This handoff is committed after implementation.

**Tests AUTHORED / NOT EXECUTED:** RTS unit now presses W for IdleCameraPan, captures an unrelated MouseMove through RmlUi (gameplayAllowed=false), verifies the keyboard Action stays active, then releases W. Existing script override, RTS controls, pointer rebinding, contexts, focus loss tests remain. There is no CI-green or native GUI claim.

**Static audit scope:** read FPS InputFlow project/profile F10 code, RTS GameClient event/frame, TopDown, Planet, InputRouting, ClientHost focus bridge, direct editor GameWindow and editor InputWindow. Project-owned dynamic ability rules and UI text must remain presentation/genre rules, not deterministic input library logic. **Remaining source-level gap:** direct editor GameWindow uses `boundButton` + ImGui physical `IsMouseClicked/Released/Down`, so advanced InputFlow pointer Mapping triggers and Chord/NotChord gates are not evaluated for direct RTS play; hosted-client path is separate and already forwards to InputFlow. Further review needed before claiming full parity.

**Next agent — Stage 53:** align direct `apps/editor/windows/GameWindow.cpp` pointer sampling with InputFlow's Action values/triggers and preserve click/box/drag semantics, especially script-card input before `input(ed)`. Add focused native/automated tests where practical; do NOT regress the hosted ClientHost path. Document API, tests NOT RUN and next release gate. Later stage: test full target builds and CTest, repair failures, rebase/merge inspector integration changes only after verification.


## Stage 53 — semantic pointer actions in editor direct GameWindow (2026-11-18)

**Status:** IMPLEMENTED AT SOURCE LEVEL; compiler/CTest/desktop GUI/CI NOT RUN per user instruction to finish implementation before running tests. Feature branch `feature/aiv990_enhanced_input`, PR #20 still Draft.

**Problem:** `apps/editor/windows/GameWindow.cpp` previously sampled only keyboard through InputFlow and looked up physical Select/Command mouse bindings to call `ImGui::IsMouseClicked/Down/Released`; authored Chord/NotChord and temporal Pressed/Tap/Hold/DoubleTap/Pulse conditions were bypassed. Wheel zoom bypassed the project-authored `CameraZoom` Action. Script-card clicks ran before `updateCameraActions`, preventing Action evaluation at that stage.

**Code/API changes:**
- `libraries/include/aiverselib/gameplay/EnhancedInput.h`: public read-only `const Action* InputFlow::actionDefinition(const std::string&)` for inspecting authored trigger declarations in host/editor presentation code; no mutation API added.
- `apps/editor/windows/GameWindow.cpp`: moves `updateCameraActions` to `GameWindow::draw` after authored project asset refresh and before `scriptCard`; removes the duplicate later call in `input(ed)`. One InputFlow frame samples deduplicated keyboard/mouse buttons, mouse axes and horizontal/vertical wheel. Focus/text capture still cancels its local InputFlow. `actionMouseClicked` resolves Action-level and Mapping-level trigger kinds: for edge/timed mappings it uses the `Triggered` event; for continuous bindings it uses the first physical click with an eligible ActionState; `actionMouseDown` checks effective eligibility. Physical mouse-up still closes a drag even if its Action becomes ineligible. `cameraWheel()` reads semantic `CameraZoom` (legacy raw fallback only if no RTS.Pointer context), including the direct region-camera path.
- Does not synthesize cursor clicks from gamepad/keyboard-only Select actions; preserves ImGui pointer ownership, click-box/rally/skill-card world spatial semantics and existing validated Commands. Hosted `ClientHost` play is untouched and keeps forwarding its own input exactly once.

**Files/commits:** core getter `cd7b72c770ab31de44a649f6731ed496a6a75f61`; editor sampling `46ce6e03b1b5c3e0b00ffaa2c9ec133437bf7872`, explicit set include `d8b6e1a19045b1081b65d2374ebd8fd56b41128e`; core getter assertion `094f72a89c26d77d06f1947b3cdd20b18bcab65b`; docs `43176b79cac90cd600b9d3cb86a7e66da5119077`; agent skill `3776414dbd28bf0cc473d7c1672a4235b93b5fc4`; this handoff follows.

**Tests AUTHORED / NOT RUN:** `enhanced_input_tests.cpp` now validates getter presence/type and missing definition. Prior core/RTS/adapter/config cases are present. No ImGui-native GUI smoke or editor compile executed. Later release verification must cover real script cards, drag box, wheel remapping, left/right swap, non-default mouse buttons, Tap/Hold gates, viewport focus loss and hosted mode separately.

**Risks/limitations:** Advanced mouse trigger policies in the direct editor now determine initiation via Triggered, but drag release stays tied to the mapped physical mouse; editor spatial interactions remain presentation-level and cannot translate pure keyboard/gamepad actions into cursor coordinates. All cross-platform compiler/API/API usage is unverified until the test phase. If an authored Action has a semantic gate bound to a second device, the editor must feed that second device or explicitly leave the interaction unsupported (no raw-key fallback through a modal).

**Next agent — Stage 54:** perform a final source-level audit of InputFlow core C++ compilation hazards, especially conditional-chord bounded convergence, trigger time/release semantics, all new API names, editor pointer calls and tests; write corrective source edits and a release gate in handoff. When authorized, run full native compiler/CTest/GUI/CI and keep PR Draft until passing evidence. Do not infer tests from committed files.


## Stage 54 — unified Action/Mapping chord consumption and scalable bounded resolution (2026-11-18)

**Status:** IMPLEMENTED IN GITHUB SOURCE, regression tests AUTHORED; compiler/CTest/editor-native/UI/gamepad/touch/CI NOT RUN per user's implementation-first instruction. PR #20 stays Draft, source branch `feature/aiv990_enhanced_input`.

**Defect:** Stage 48/47 preview reconciled consuming mappings gated by Mapping-level Chord/NotChord, but skipped the **Action-level** Chord/NotChord from control arbitration. A high-priority physical mapping could consume a mouse button even when its Action-level `Chord("Gate")` was false, suppressing a lower context incorrectly. The preview refinement also hard-coded eight iterations even when a valid transitive dependency chain had more than eight Actions.

**Implementation:** `libraries/include/aiverselib/gameplay/EnhancedInput.h::InputFlow::update` detects conditional chord gates declared on either Mappings or Actions. The effective candidate rebuild checks both Mapping and Action trigger chord constraints; Pass 2 uses the same conjunction for eligibility and consumption. The convergence iteration maximum scales with Action count + 1 (capped at 512 for pathological native graphs), preserving the existing deterministic conservative fallback for cyclic consumption feedback. The Action-dependency DAG validator introduced in Stage 47 remains in place; authored JSON schema, stable profile IDs and deterministic Commands/ECS are unchanged.

**Source commits:** `libraries/include/aiverselib/gameplay/EnhancedInput.h` `5252fc8414b4ad30267f92d3790547b12b953f39`; `tests/libraries/enhanced_input_tests.cpp` `741b8953cb358cd9d7d003d61549fc60479a8dd2`; `docs/enhanced-input.md` `69a1ae5f3155fae75f7faeed034002ef08bdaf1a`; `skills/ui/aiverse-input/SKILL.md` `91b187a396d98e9272e45a1d93d1b0edb8bff8f4`; this handoff commit follows.

**Tests AUTHORED / NOT EXECUTED:** a high-priority Action-level Chord-gated `High` and lower `Low` share mouse0. With Gate absent, Low triggers; holding Gate switches to High, releasing Gate restores Low. Also a 12-Action DAG chain validates non-lexical order and deep prerequisites. Prior Mapping gate, Hold/Pulse, context reactivation, editor getter, adapter device ownership, JSON cycle tests are retained. No compiler PASS is claimed.

**Known limitations/risk:** A priority/consumption feedback graph may still lack a logical fixed point even if the Action Chord dependency DAG is acyclic; the conservative fallback is deterministic but not a SAT solver. Very large native graphs can run more refinement passes; performance/latency is unmeasured. Direct editor GameWindow advanced trigger plumbing is source-only and must be compiled/GUI-tested. CI and CTest pending.

**Next agent — Stage 55 (release gate):** inspect latest branch head, verify native C++ names/signatures, CMake aliases, project JSON/profile compatibility, consistent public docs, and source/test compile issues. With user authorization for a test phase: run the four InputFlow unit binaries, editor/RTS/FPS targets, full regression `./run-tests.sh`, macOS/Linux/Windows workflow, and manual iOS/Android/web touch/pad profiles. Capture exact SHA, commands, PASS/FAIL, warnings, fixes. Keep PR #20 Draft until evidence is green. Merge the current inspector branch safely before final PR review (its upstream has continued moving), without force-pushing or overwriting other agents' histories.


## Stage 55 — final source contract audit and explicit verification gate (2026-11-18)

**Status:** SOURCE IMPLEMENTATION PREPARED; build, linker, CTest, native GUI, gamepad/touch devices and CI **NOT RUN**. This is a documentation/static verification step, NOT a successful build. PR #20 remains DRAFT. Working branch: `feature/aiv990_enhanced_input`; branch HEAD entering this stage: `f75408d08ae2222a3d4167386ec482f77227b55e`.

**Source audit performed:** fetched the actual GitHub branch files and confirmed (string/API-declaration presence, NOT C++ type checking) that core `InputFlow` exports `actionDefinition`, `cancelDevices`, `suppressUntilRelease`, cycle validation and `InputSystem` compatibility; canonical public header exports `inputflow::InputFlow`; `InputFlowAdapter` implements selective keyboard/pointer/touch cancellation; `InputFlowUsers` exposes owner unassignment; RTS `ClientInputRouting` uses device-scoped UI capture; `GameClient` filters legacy keyboard mirror; direct editor GameWindow samples mouse/keyboard/wheel actions before script cards and uses Action trigger declarations; `libraries/CMakeLists.txt` provides `AIVERSE::InputFlow` alias; `tests/libraries/CMakeLists.txt` declares all four `aiverse_enhanced_*_tests` executables. Nine file contract checks found zero missing searched declarations. This is **not** proof of linkability, ABI, C++ build, CI or working GUI.

**Documentation correction:** `docs/enhanced-input.md` canonical C++ quickstart now includes `<aiverselib/gameplay/InputFlow.h>`, uses `inputflow::InputFlow` and `platform::InputFlowAdapter` instead of the historical aliases. Backwards compatibility remains supported. Change commit `f75408d08ae2222a3d4167386ec482f77227b55e`.

**Implementation summary:** Stages 1–46 assembled device adapters/contexts/actions/modifiers/triggers/presets/JSON/profiles, FPS/RTS/TopDown/Planet/Possession/editor clients, canonical InputFlow naming and CMake target. Stages 47–54 addressed transitive/cyclic Chord/NotChord, gated control priority and Action-level consumption, canceled lifetimes/timer resets, capture and preactivation edges, multi-user device ownership, RTS RmlUi/legacy key leaks, and direct editor GameWindow semantic pointer/wheel handling. Each implementation stage has its preceding documented commits and authored tests. No direct ECS sim/serialized gameplay Command changes were introduced in these stages.

**Explicit release verification to perform later when user requests tests:**
1. Use a clean checkout of the exact branch head, or a dedicated worktree; inspect and safely merge current `feature/universal-entity-inspector-fps` upstream before integration. Avoid overwriting concurrent changes from animation/VFX/texture/water agents.
2. Configure/build four InputFlow standalone tests: `cmake -S tests/libraries -B build/enhanced-input -DCMAKE_BUILD_TYPE=Debug`; `cmake --build build/enhanced-input --parallel 2 --target aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests`; `ctest --test-dir build/enhanced-input --output-on-failure -R '^library_enhanced_(input|rts_input)'`. Capture exact compiler version, exit codes, CTest results and Git SHA. On multi-config Windows use `--config Debug` and `ctest -C Debug`.
3. Build the desktop editor, RTS and FPS clients and run `./run-tests.sh` for full deterministic ECS/RTS/FPS regression parity. Do not assume the focused suite covers platform renderer or integration.
4. Manually test FPS F10 input settings: rebind Allow/Reject/Swap, restart persistence, two-sided swap, profile reset, invalid profile/assets, per-player ownership and hotplug. Verify input/actions.json schema v1 and old `AIVERSE::lib_input` includes still work.
5. Editor direct-play GUI: Select/Command bound LMB/RMB/X1/X2, Hold/Tap/DoubleTap/chord mapping, click/box/drag release, script cards, wheel remapping, text focus/modal capture and hosted-client focus transfer. Verify actionMouseClicked semantics under a blocked UI and no duplicate input updates.
6. Check RTS semantic command actions, script-owned hotkeys, TopDown abilities/vehicles, Planet pinch/pan/twist, possession, iOS/Android/Web touch and pad; respect one backend poll and tick-quantized Commands. Run the `.github/workflows/enhanced-input.yml` macOS/Linux/Windows job and inspect artifacts/logs.
7. Document every actual PASS/FAIL and fixed compiler/runtime issue in a new Stage 56. Keep PR #20 Draft until all required checks pass. Only then consider marking ready/merging.

**Known unverified risks:** C++ alias/header ordering across toolchains; ImGui symbols and editor event ordering; transient gesture lifetime; profile disk I/O; long conditional-chord graphs and feedback fallback performance; local multi-user hotplug; platform-specific focus/IME behavior. These require real test evidence, not assumptions.

**Next agent task (Stage 56):** start with this Stage 55 handoff, read root AGENTS.md and `skills/ui/aiverse-input/SKILL.md`, capture current branch SHA and upstream relationship, run the dedicated validation phase when authorized, fix failures, commit code+stage documentation together, update PR #20 test evidence. Do not claim production-ready until native + cross-platform validation has passed.


## Stage 56 — first real GitHub Actions evidence and Release test correction (2026-11-18)

**Branch/entry SHA:** `feature/aiv990_enhanced_input` at `cf26d02d0343c6060632685d9f188d2088272786`. **Status:** targeted InputFlow tests on Ubuntu, macOS and Windows were **QUEUED / NOT RUN** at inspection; one broad Linux ARM64 CI job **FAILED** for proven Release-only warnings outside InputFlow. Source fixes applied, but the post-fix validation jobs are **QUEUED / NOT CONFIRMED PASS**. Do NOT mark PR #20 ready.

### Evidence collected (actual remote CI, not a local build)
- GitHub Actions run `44305537614`, workflow `AIVerse InputFlow targeted checks`, jobs: Ubuntu `134806003972`, macOS `134806004347`, Windows `134806004751`. All three reported **queued**, conclusion null when queried; there is no compiler/test evidence yet.
- GitHub Actions full CI run `44305538135`, **Linux arm64 gcc** job `134806075873` reported **completed/failure**. Compiler invocation showed `g++ -O3 -DNDEBUG -std=c++20 ... -Werror`. Its complete downloaded job log identifies failures compiling **only** `tests/ecs_wave_executor_test.cpp` and `tests/ecs_experiments_test.cpp` at that point: unused variables `expected`, `failed`, `reused`, `caught`, `indexOf` and unused helper functions `execute`, `result`. No InputFlow compiler failure was reported in that job; the full build stopped before completion, so no InputFlow success can be concluded.
- Latest inspector branch diverged from this feature branch (`compare_commits`: 116 commits ahead in inspector, 303 commits behind at the time of inspection). Upstream changed `CMakeLists.txt` and `AGENTS.md` plus SpectraFX editor/VFX files. Do not force-push/rebase without a controlled merge and conflict review.

### Correction made
- `tests/ecs_wave_executor_test.cpp`: replaced eight ordinary `assert(...)` statements with an always-on variadic `CHECK(...)` macro that aborts on failure and prints source location. This preserves test execution and side effects when Release defines `NDEBUG`.
- `tests/ecs_experiments_test.cpp`: replaced 29 ordinary asserts with the same form of `CHECK(...)`; includes explicit `<thread>`. Both files no longer lose their test-only helpers/variables to `-DNDEBUG`, so the exact reported unused warnings should be resolved without disabling `-Werror`.
- Commits: wave fix `af9e1ad20ad191f16f9359864cfaa16454a3b014`, experiments fix `4e8857b55dac076ee06b30abb2668201beca9d4b`, macro diagnostic cleanup `0922899b9dcf14f3fb677b0f68fe8a854bbc2b20` and `a01b3638a0350c690c34397320820c71f7c56b8c`, explicit `<thread>` include `357ad7e7b31cea978182e83f21775e8fdc1d1e73`.
- `docs/enhanced-input.md` Stage 56 verification section added in `d8857b9f5ed8a13a870ab5633cee072763124f8b`; `skills/ui/aiverse-input/SKILL.md` appended status in `29155b67c3f0c97b1dfe1879da4ca72a869e7cbe`.

### What is NOT verified
1. The post-fix ARM64 Release CI has not reported success. Fixing `assert` diagnostics may reveal new runtime failures previously masked by `NDEBUG`; do not assume PASS.
2. The focused four InputFlow test binaries remain queued in GitHub Actions. No successful CTest for Ubuntu/macOS/Windows is recorded.
3. Editor, FPS/RTS runtime, touch/gamepad, per-user profiles, and entire ECS determinism suite still need actual build and execution results.
4. The tool execution container cannot clone GitHub directly (network hostname resolution fails), so no local build was run from its sources. All evidence here comes from authentic GitHub Actions job metadata/log content.

### Next agent — Stage 57
- Recheck the **latest commit SHA**, GitHub Actions runs associated with it (not stale prior SHA), and their individual job logs. Fix any new failing assertions, C++ type errors or tests in the same branch and document exact PASS/FAIL outputs.
- When targeted jobs report successful CTest, run/inspect full studio/editor + FPS/RTS native CI and test per-player rebinding/profile persistence, context switching, modal capture and gamepad/touch paths.
- Merge the most recent inspector branch into InputFlow only after comparing `CMakeLists.txt`, `AGENTS.md` and other overlapping files; preserve both histories.
- Update PR #20 as Draft with actual verification evidence; only mark ready after all required checks are successful. Do not use queue/pending as proof of passing.


## Stage 57 — macOS Clang preset-loop compile fix (2026-11-18)

**Status:** Fix committed on `feature/aiv990_enhanced_input`, **local rebuild/CTest NOT YET RUN**. User reported a real macOS C++20 Clang compile failure in `aiverse_enhanced_input_config_tests`: `tests/libraries/enhanced_input_config_tests.cpp:115:20: error: loop variable 'preset' creates a copy from type 'aiverse::platform::enhanced_config::Asset const' [-Werror,-Wrange-loop-construct]`.

**Correction:** `tests/libraries/enhanced_input_config_tests.cpp` line 115 changed `for(const auto preset:{fpsReference(),rtsReference()})` to `for(const auto& preset:{fpsReference(),rtsReference()})`. This references the const temporary Asset within the initializer-list lifetime instead of copying each substantial Asset. No test assertions, preset contents or runtime input semantics changed. Commit `5ae42b96526b637d86cf1029451d3a9f553003dd`.

**Source-level audit:** inspected the four focused `enhanced_*_tests.cpp` files for similar `for(const auto variable : ...)` copies and found no additional matches. No compiler re-run was performed by this agent. The earlier Stage 56 full-CI Release fixes remain separate.

**User verification command (macOS):**
```bash
git fetch origin && git switch feature/aiv990_enhanced_input &&
git pull --ff-only origin feature/aiv990_enhanced_input
cmake --build build/enhanced-input --parallel 4 --target \
  aiverse_enhanced_input_tests aiverse_enhanced_input_adapter_tests \
  aiverse_enhanced_input_config_tests aiverse_enhanced_rts_input_tests
ctest --test-dir build/enhanced-input --output-on-failure -R '^library_enhanced_(input|rts_input)'
```

**Next agent:** collect the user's full new compiler/CTest output and exact HEAD, fix the next concrete failure while preserving API/serialization. Record actual PASS/FAIL. Keep PR #20 Draft pending full platform and editor/FPS/RTS checks.


### Stage 57 — local macOS verification reported by user (2026-11-18)

**Actual user-provided evidence:** After the Clang range-for correction, the user pasted the result of `ctest --test-dir build/enhanced-input --output-on-failure -R '^library_enhanced_(input|rts_input)'` from `/Users/aleksandrfeer/Projects/AIVERSE/build/enhanced-input`:

| Test | Result | Duration |
| --- | --- | --- |
| `library_enhanced_input` (#27) | **Passed** | 0.04 sec |
| `library_enhanced_input_adapter` (#28) | **Passed** | 0.04 sec |
| `library_enhanced_input_config` (#29) | **Passed** | 0.60 sec |
| `library_enhanced_rts_input` (#30) | **Passed** | 0.47 sec |

**Observed outcome: 4/4 focused CTest suites passed on the user's local macOS machine.** Successful execution means the four test binaries existed and ran, but the user's snippet did not include the precise `git rev-parse HEAD`, compiler version, build log or full test-suite output; do not attribute an exact tested SHA or claim full native-editor/FPS/RTS certification. The fix commit in the branch is `5ae42b96526b637d86cf1029451d3a9f553003dd`; handoff before this record was `e60f7b56af735a31eb2230545e9c9ff1e9b95997`.

**Latest CI status at inspection:** GitHub Actions on `e60f7b56af735a31eb2230545e9c9ff1e9b95997` had workflow `44312460446` (InputFlow targeted), `44312460447` (Studio), and `44312460421` (full CI) **queued**, not PASS. The input branch is still divergent from `feature/universal-entity-inspector-fps` (ahead 309, behind 136 at inspection). Do not merge without resolving current upstream changes and compiling/testing the integration.

**Next validation step:** On the user's macOS checkout run `./run-tests.sh --jobs 4 --ctest-jobs 4` (Debug, editor + comprehensive test build), and record `git rev-parse --short HEAD`, the latest `build/all-tests/test-reports/*/summary.txt`, and compiler/CTest errors if any. If this passes, run the separate native FPS + RTS client builds and GUI/play-device smoke; then inspect current multi-platform CI. PR #20 remains Draft until those additional checks are green.


## Stage 58 — editor InputFlow window access modifier compile fix (2026-11-18)

**User-reported macOS compiler evidence:** The full `./run-tests.sh` Debug editor build reached approximately 95%, then Clang failed to compile `apps/editor/windows/InputWindow.cpp:139`: `'isWindowInFront' is a private member of 'aiverse::editor::Editor'`. `apps/editor/Editor.h:299` declared the method private. `InputWindow::update(Editor&,double)` uses it to cancel pending keyboard rebinding capture when the docked InputFlow window loses its front tab. The full test runner reported `FAIL build (exit 2)` and stopped before CTest.

**Fix:** Expose the existing `bool Editor::isWindowInFront(EditorWindow* w) const` declaration in the public windows API in `apps/editor/Editor.h`; remove only the duplicate private declaration. Preserve `Editor.cpp` implementation, `InputWindow` logic, editor keyboard capture semantics, input mappings and ECS Commands. The editor already called this helper internally for hosted-client focus routing, so its semantics are unchanged. Code commit: `570f6ae627c8023b39a39190030c66f458f58af2`.

**Validation:** change committed, **macOS rebuild NOT YET RUN** after fix. Previous four dedicated InputFlow CTest suites passed on user's macOS; the full editor build had not passed at the reported SHA. No CI/GUI PASS is claimed for this fix.

**Next local step:** after `git pull --ff-only origin feature/aiv990_enhanced_input`, run `./run-tests.sh --jobs 4 --ctest-jobs 4`; because the test runner configures and builds, this will recompile the changed editor header and run CTest only if compilation succeeds. If it still fails, record first compiler error plus latest `build/all-tests/test-reports/*/build.log`. After all tests pass, validate direct editor InputFlow Actions capture and hosted-client focus. Keep PR #20 Draft until full verification.


## Stage 59 — scope RTS MouseWheel switch case (2026-11-18)

**User-reported compiler failure:** Full macOS Debug editor build reached 98%, then Clang rejected `apps/client/GameClient.cpp:726`: `cannot jump from switch statement to this case label`, due to `const auto* zoom` initialized directly under `case EventType::MouseWheel` without a scoped block. It also reported errors at subsequent `TouchBegin`, `TouchMove`, `TouchEnd`, `KeyDown` and `default` labels, all caused by the same unscoped declaration. `./run-tests.sh` reported `FAIL build (exit 2)`; full CTest was not reached.

**Fix:** In `apps/client/GameClient.cpp::event`, surround the semantic wheel branch with `case EventType::MouseWheel: { ... break; }`. No changes to CameraZoom mappings, presentation zoom scaling, InputFlow API, serialized Commands or ECS simulation. Source commit `becf55ed62250db86f7ad26561aa5c2e4b5da080`.

**Evidence:** Exact user-supplied macOS Clang diagnostic and source inspection. Updated code is committed, but **a successful post-fix editor compilation/full test run is NOT YET confirmed**. Prior four focused InputFlow CTest suites were reported PASS by user; this does not prove the full editor binary is built.

**Next agent / user validation:** pull `feature/aiv990_enhanced_input` and rerun `./run-tests.sh --jobs 4 --ctest-jobs 4`; if the build fails, capture first compiler diagnostic and the build log. Preserve `isWindowInFront` public access fix from Stage 58. If full build/tests pass, record exact HEAD, test summary, and proceed to editor InputFlow Actions tab capture + gameplay smoke. Do not mark PR #20 ready without integration evidence.


## Stage 60 — macOS Clang planet-camera switch-case scopes (2026-11-18)

**User-reported build error:** Mac Debug editor build reached 92% and failed in `apps/client/ClientPlanet.cpp`. The `planetCameraEvent()` switch declared `const auto* wheel` and `const float value` directly under `case EventType::MouseWheel`, followed by `const auto fired = [...] ` directly under `case EventType::KeyDown`. Clang reported `cannot jump from switch statement to this case label` at subsequent `KeyDown`, `TouchMove` and `default` labels. All three reported errors share the unscoped local-initialization cause.

**Fix:** `apps/client/ClientPlanet.cpp`: wrap `case EventType::MouseWheel: { ... }` and `case EventType::KeyDown: { ... }` in explicit scopes. No changes to wheel zoom, `PlanetNextBody`, `FocusCamera`, touch camera controls, InputFlow Actions, ECS Commands or gameplay semantics. Code commit `9d4ecfba4f1e64ad7bcdc166c5c23c35fdcb9a5c`. This mirrors the earlier Stage 59 scoped `GameClient.cpp` wheel correction.

**Verification:** exact user-supplied local compiler diagnostics plus source-level correction; **post-fix local compilation/CTest not yet confirmed**. Four focused InputFlow suites previously passed (Stage 57). The editor's complete Debug build still needs a fresh user run; keep PR #20 Draft.

**Next agent/test:** `git pull --ff-only origin feature/aiv990_enhanced_input` then `./run-tests.sh --jobs 4 --ctest-jobs 4` from the repository root. Capture the next first compiler error or PASS summary. If the build succeeds, conduct a native editor InputFlow Settings capture and planet/RTS/FPS smoke. Preserve exact branches and per-stage handoff updates; do not claim a successful full build from a source-only fix.


## Stage 61 — fix skill catalog and input skill path audit (2026-11-18)

**User-reported CTest failures (actual, from full macOS test run):**
- `skill_catalog` (CTest #90) **FAILED**: `catalog.py: error: stale skill catalog; run generate`.
- `skill_audit` (CTest #92) **FAILED** with nine `missing path` reports in `skills/ui/aiverse-input/SKILL.md`: `apps/fps/client.cpp`, `docs/enhanced-input-agent-handoff.md`, `docs/enhanced-input.md`, `docs/gameplay-settings.md`, `engine/platform/EnhancedInputAdapter.h`, `engine/platform/EnhancedInputConfig.h`, `libraries/catalog.json`, `engine/gameplay/Input.h`, `libraries/spec/gameplay.json`. These were caused by literal escaped-backtick Markdown (`\\`path\\``) in the skill. The repository audit interpreted the leading backslash as part of a path.
- Remaining tests in the user's CTest output were not provided, so do not claim that the full 106-test suite has passed.

**Changes:**
1. `skills/ui/aiverse-input/SKILL.md`: replaced 116 literal `\\`` sequences with standard Markdown backtick delimiters. This makes the nine existing references parse as real paths, without hiding any path errors or weakening the path validator. Commit `e6f2b370368fe991fb966e2f020d866b5e844c37`.
2. `skills/catalog.json`: synchronized the `aiverse-input` entry with the canonical input package in `libraries/catalog.json`: `summary` now describes InputFlow actions/contexts/modifiers/triggers/profiles and the `source` points at `libraries/include/aiverselib/gameplay/InputFlow.h`. These are precisely the two fields the current deterministic catalog generator expects to change. No catalog IDs, dependencies or package contracts changed. Commit `77f29dd83e7d5cec280645d82662832feb0b29ec`.

**Verification evidence:** GitHub file inspection confirmed that all nine paths exist on the feature branch, the skill contains zero remaining `\\`` escape sequences, and the generated catalog entry agrees with the library's package metadata. GitHub commit diff for `skills/catalog.json` shows only the two stated field changes. **The local Python scripts and CTest have NOT been rerun after these corrections; no PASS is asserted.**

**Next check (macOS, current branch):**
```bash
git pull --ff-only origin feature/aiv990_enhanced_input
python3 tools/skills/catalog.py check
python3 tools/skills/audit.py --skills aiverse-input
ctest --test-dir build/all-tests -C Debug --output-on-failure -R '^(skill_catalog|skill_audit)$'
```
The full `./run-tests.sh --jobs 4 --ctest-jobs 4` suite must be rerun/confirmed separately; follow any new failures using the first error and existing reports. Keep PR #20 Draft pending full editor/FPS/RTS, platform and input UI smoke checks.

**Next agent:** continue from this stage, verify exact Git HEAD and real PASS/FAIL, and preserve `input/actions.json`, legacy input and ECS command invariants. Update this handoff after the next confirmed result.


## Stage 62 — full macOS Debug build and 106/106 CTest verified (2026-11-18)

**User-provided primary evidence:** `build(4).log`, `configure(4).log`, `ctest(3).log`, `ctest(2).xml`, `summary(4).txt`; exact tested Git HEAD `2b15d49b` (`2b15d49bde632b39334ee9c83b54e1a5fa296282`). macOS; full Debug build `build/all-tests`.

**Actual results:** CMake configure PASS; build PASS through `aiverse_editor` and `aiverse_tests`; CTest **106/106 Passed, 0 failed**, duration 865.89 seconds. Previously failing `skill_catalog` and `skill_audit` both PASS. FPS, FPS transport, ECS rollback and determinism, RTS/net/headless, native project linking, editor suite and game tooling also passed. The uploaded `summary(4).txt` confirms `PASS configure`, `PASS build`, `PASS ctest`. The report does not cover the extra suites after CTest (no PASS evidence for them in the provided summary). Some optional gRPC SecureTransport TLS subcases were skipped (not whole CTest failures).

**Limitations:** This macOS result applies specifically to `2b15d49b`; later ECS test changes below are not covered by this run, and interactive GUI/pointer/gamepad/touch flows are still not manually verified. Avoid describing Windows/Linux CI as green without run conclusions.

## Stage 63 — Linux ARM64 Release `-DNDEBUG -Werror` regression fixes and main integration request (2026-11-18)

**Real CI evidence:** GitHub Actions run `44316023837` Linux ARM64 GCC job `134842276701` completed FAILURE while compiling tests with `-O3 -DNDEBUG -std=c++20 -Werror`. Failed sources and diagnostics: `tests/ecs_snapshot_delta_test.cpp` (unused `mismatch`); `tests/ecs_guarded_world_test.cpp` (unused `state`, `caught`, `run`); `tests/ecs_scene_hierarchy_test.cpp` (unused `a`, `c`, `root2`, `stale`, `reused`, `result`, `dot`, `parent`, `child`, helper `x`). Their assertions had vanished under `NDEBUG`.

**Corrections committed:** Replace standard `assert` with always-on `CHECK` preserving test semantics and side effects without weakening `-Werror`: 7 assertions in snapshot delta (`11a21a35f02939c797019eb53b5d1fe53e7d3d08`), 14 in guarded world (`b7ae90443d15ed85bfed347407cb9e174dcdc92a`), 72 in scene hierarchy (`e32595d6efb16aaf408082d51ea8e7195d8f6775`). This is 93 checks across three regression files; prior Stage 56 fixes cover the other two ECS test sources.

**Verification:** Static review of the changed test file headers and error-line causes. No successful post-fix ARM64 CI or current-HEAD macOS full CTest observed. **NOT RUN / PASS not established** on these new commits. Scope is ECS tests, not InputFlow runtime or serialized Commands.

**Integration request:** User explicitly requested merge to `main`. Before doing so, verify current GitHub base/head; distinguish the macOS full green result at `2b15d49b` from subsequent test-only corrections, record PR/merge SHA and actual remote checks. Do not rewrite main history or silently discard other agents' changes. Maintain input-independent ECS test fixes, preserve stage handoff documentation, and leave new platform verification as follow-up when unconfirmed.


## Stage 64 — merged into main through PR #20 (2026-11-18)

**Completed operation:** GitHub pull request [#20](https://github.com/chromealex/AIVERSE/pull/20) was retargeted to `main`, moved out of Draft at the user's explicit instruction to integrate, and successfully merged via GitHub merge method (no force update). **Merge commit:** `fc4e328074f6f5da91d0103810d74737bc2328c5`. The complete `feature/aiv990_enhanced_input` history remains reachable; the branch head integrated was `78419438f9439e43ef9465f3089bf8fef0e4b5f7`. GitHub compare `feature/aiv990_enhanced_input...main` after merge showed `ahead 1, behind 0`, confirming the branch was fully integrated and main advanced only by the merge commit at that point.

**Verified evidence carried into integration:** user-provided macOS Debug configure/build/CTest PASS at `2b15d49b`, `106/106` CTest successes and zero failures, full editor binary built. GitHub Linux ARM64 Release check `134842276701` failed before three test-only `NDEBUG` fixes in `tests/ecs_snapshot_delta_test.cpp`, `tests/ecs_guarded_world_test.cpp` and `tests/ecs_scene_hierarchy_test.cpp`; Stage 63 records their original diagnostics and the source commits. **The macOS test evidence predates these test-only fixes**; no successful Windows/Linux post-merge run, mobile smoke or GUI pointer/gamepad/touch regression was observed or claimed.

**Next agent responsibilities:** (1) use `main` as the integration base; (2) check post-merge CI job conclusions and fix outstanding cross-platform failures on fresh isolated feature branches + handoffs, not via history rewriting; (3) smoke-test InputFlow Settings/F10, editor InputFlow Actions, host focus, keyboard+mouse/pad/touch and rebind profile save/reload; (4) preserve InputFlow genre-neutral C++ API and ECS Command separation. This handoff update records the merge and is a documentation-only follow-up, not a new runtime implementation or a passing additional test.
