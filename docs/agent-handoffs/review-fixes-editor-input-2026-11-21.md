# Review fixes: editor core + InputFlow + agent link (2026-11-21)

Branch `feature/aiv20261121_fix_editor_input`, base origin main `973b32970`. Not pushed, not merged.

## Commits

| SHA | Subject |
|---|---|
| `624d04d` | InputFlow action-level release triggers, DoubleTap, cancelAll events |
| `0d9b5d9` | one undo snapshot per FPS gizmo drag and per name/id text edit |
| `7c346cf` | legacy light pos:[x,z] read, Transform rotationY+position write |
| `cb91ca9` | Lua Custom Inspector UAF, sandbox, per-frame undo snapshots |
| `7d9a60e` | authenticate the editor agent link (token, line cap, HTTP reject) |
| `11412cf` | shader graph editor scripts send the agent-link hello (outside the area, additive) |

## Findings

| # | Sev | Status | Files | Notes |
|---|---|---|---|---|
| 1 | HIGH | FIXED | `apps/editor/windows/LuaCustomInspector.h` | array widget iterates a copy; element loop stops after "-". |
| 2 | HIGH | FIXED | same | All script calls go through `invoke()` -> `lua_pcall(callProtected)`: raw global lookup, args built and `lua_call` inside pcall with budget hook + memory cap. Results read raw only (`rawField`, `lua_next`, `lua_rawgeti`). `setmetatable` wrapper rejects `__gc`. Capped `lua_Alloc` (64 MiB, script allocations only; host pushes of document data uncapped). `lua_atpanic` handler (logs; by construction nothing raises outside pcall except real OOM). `luaJson` bounded at 65536 nodes (self-referencing tables). |
| 3 | MED | FIXED | same | slider/drag/vector/color: first write of a drag snapshots, later writes merge while `liveKey_` matches and `doc.revision` is unchanged; reset on `IsItemActivated` and when no item is active. |
| 4 | MED | FIXED | `apps/editor/windows/SceneWindow.cpp` | `fpsDragSnapshot_`: one `beginEdit()` per drag regardless of write results. |
| 5 | MED | FIXED | `libraries/include/aiverselib/gameplay/EnhancedInput.h` | `Aggregate::permit`: trigger-less eligible mapping permits when down/pressed/released (or a triggered mapping fired); `triggered = permit && actionGate` for actions with triggers. |
| 6 | LOW | FIXED | same | DoubleTap firing clears `releaseArmed` and skips the next arm (`skipNextArm`); release arms only if the first press lasted <= the window. |
| 7 | LOW | FIXED | `apps/editor/DetailsPanel.{h,cpp}` | displayName and `##id`: one snapshot per edit session (`textEditSnapshot_`, reset on activation); `##id` snapshot taken from the pre-edit value. |
| 8 | LOW | FIXED | `apps/editor/EditorEntityAdapter.h` | light reads `pos:[x,z]` first (as `RtsLights::parseMapLights`); rotationY+position patch writes both. Kept the existing tested rule "objects without authored facing must not invent it": without facing only the position part is written. |
| 9 | HIGH | FIXED | `apps/editor/core/Net.{h,cpp}`, `apps/editor/EditorAgent.cpp`, `apps/editor/Editor.h`, `tools/mcp/main.cpp`, `docs/mcp-agent-link.md`, `docs/overview.md` | (a) 4 MiB line / unterminated buffer cap -> drop; (b) HTTP request lines -> drop and discard the client's lines; non-JSON line -> drop (editor side); (c) per-session 64-hex token in `~/.aiverse/agent-link/<port>.token` (0700/0600, `AIVERSE_AGENT_LINK_DIR` override), removed on close; first message must be `hello` with the token (constant-time compare); aiversemcp `connectAndHello()` re-reads the token on every connect. No backward compat: there was no discovery file (the port is the discovery), old aiversemcp gets an "update aiversemcp" error. Protocol: `docs/mcp-agent-link.md`. |
| 10 | LOW | FIXED | `LuaCustomInspector.h` | text caches cleared on `doc.revision` change (not while an item is active); `validate()` uses `lua_type == LUA_TSTRING`; `lua_checkstack` in `pushJson`, `luaJson`, `widget`. |
| 11 | LOW | NOTED | `apps/editor/core/WorldInspect.cpp` | `register/unregisterWorldInspectorExtension` confirmed unused; left as is. |
| 12 | PERF/OPT | PARTIAL | `EnhancedInput.h` | cancelAll lost-events FIXED: Canceled phases are also returned by the next `update()` unless `events()` was read first. Per-frame allocation / chord loop perf: DEFERRED (optional, not low-risk to restructure without benchmarks). |

Note: `LuaCustomInspector.h` is not included by any editor TU at the moment (dead until wired).

## Verification actually run (Linux, g++ 14.2)

Include set: `INC="-I. -Iengine -Ilibraries/include -Ithird_party -Ithird_party/imgui -Ithird_party/lua -Iapps/editor"`.

- InputFlow: `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Ilibraries/include tests/libraries/enhanced_input_tests.cpp` -> PASS; the same test against the old header aborts at the new Tap check (line 509). Also PASS: `enhanced_input_adapter_tests.cpp` (+ `engine/platform/gamepad/GamepadCommon.cpp`), `enhanced_rts_input_tests.cpp`, `enhanced_input_config_tests.cpp` (+ Json.cpp, Fixed.cpp), all with `-Werror -pedantic`.
- Adapter: `tests/editor/scene_rotation_tests.cpp` compiled `-Wall -Wextra` (clean), linked with MapDoc.cpp/Json.cpp/Fixed.cpp objects and `-Wl,--unresolved-symbols=ignore-all` (the test calls none of the missing sim symbols) -> PASS; with the old header -> abort.
- `g++ -std=c++20 -fsyntax-only -Wall -Wextra $INC` clean (only two pre-existing `-Wmisleading-indentation` warnings in `VehicleControllers.h` / `GpuResourceRetirement.h`): `DetailsPanel.cpp`, `windows/SceneWindow.cpp`, `EditorAgent.cpp`, `tools/mcp/main.cpp` (`-DAIVERSE_SOURCE_DIR`), a TU including `LuaCustomInspector.h` and calling `draw()`.
- Lua sandbox: new `tests/editor/lua_inspector_sandbox_tests.cpp` built with the vendored Lua compiled as C (all `AIVERSE_LUA_SRC`), Json.cpp, Fixed.cpp, editor symbols unresolved-ignored -> PASS (metatable `__index` loop, `__gc` rejected, `string.rep(1<<30)` memory error with state still usable, endless loop budget, `_G` metatable, self-referencing result, 64-deep value). Also PASS under `-fsanitize=address,undefined` (Lua objects rebuilt with sanitizers).
- Net: `apps/editor/core/Net.cpp` `-Wall -Wextra -Werror` clean; new `tests/editor/agent_link_tests.cpp` (`g++ -std=c++20 -Wall -Wextra -Werror -I. tests/editor/agent_link_tests.cpp apps/editor/core/Net.cpp`) -> PASS; body of the existing `tests.cpp` `editor_mcp_ports_are_unique_and_route_to_the_right_listener` extracted and run against the new Net.cpp -> PASS.
- `python3 -m py_compile tests/shader_graph/{editor_smoke,validate_glsl}.py` -> OK.

## UNVERIFIED

Full CMake build and CTest (KINGSTON only), Windows `_WIN32` branch of Net.cpp, macOS, the live editor + aiversemcp handshake end to end, ImGui runtime behaviour of the undo changes and the Lua inspector, the shader-graph Python scripts against a live editor.

## CMake registrations needed (CI agent / integrator)

Inside the `AIVERSE_BUILD_EDITOR` test block next to `aiverse_editor_scene_rotation_tests`:

```cmake
add_executable(aiverse_editor_agent_link_tests tests/editor/agent_link_tests.cpp)
target_link_libraries(aiverse_editor_agent_link_tests PRIVATE aiverse_editor_core)
add_test(NAME editor_agent_link COMMAND aiverse_editor_agent_link_tests)
add_executable(aiverse_editor_lua_inspector_sandbox_tests tests/editor/lua_inspector_sandbox_tests.cpp)
target_link_libraries(aiverse_editor_lua_inspector_sandbox_tests PRIVATE aiverse_editor_core aiverse_lua aiverse_imgui)
add_test(NAME editor_lua_inspector_sandbox COMMAND aiverse_editor_lua_inspector_sandbox_tests)
```

The sandbox test includes `Editor.h` but calls no Editor function; if linking pulls unresolved editor-app symbols, add `$<TARGET_OBJECTS:...>` as for the editor or keep it Linux-only. The new cases in `enhanced_input_tests.cpp` and `scene_rotation_tests.cpp` need no CMake change.

## Risks

- Agent link: a request line > 4 MiB is now refused (large `game_file_write` / inline asset payloads through aiversemcp would fail; none observed in the code, but check). Old aiversemcp binaries cannot talk to a new editor (explicit error). A token file that cannot be written (no HOME) makes the link refuse every client with the path in the error.
- InputFlow semantics: for actions with action-level triggers, a mapping with its own triggers that did not fire no longer blocks if another trigger-less mapping of the same action is active (OR of mapping permits). `events()` read between `cancelAll()` and `update()` suppresses re-delivery.
- Lua inspector: a real OOM during host-side argument pushes inside the trampoline would longjmp over C++ temporaries (leak); the script-side cap makes this practically unreachable. Pathological Lua patterns (`string.find` backtracking in one C call) are not bounded by the instruction hook.

## Next step

Integrator: merge, add the CMake lines above, run the full build + CTest on KINGSTON (`editor_scene_rotation`, `library_enhanced_input*`, `editor_agent_link`, `editor_lua_inspector_sandbox`, `aiverse_tests` editor_mcp_ports), then one live editor + aiversemcp session to confirm the hello handshake and the stderr message for a stale aiversemcp.
