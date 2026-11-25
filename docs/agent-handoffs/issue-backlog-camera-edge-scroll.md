# Camera edge scrolling (#128)

Accept default parity, project-configured zone, dwell and speed, cancellation outside the window and on focus or screen changes, and independent keyboard and gamepad pan.

Implementation 39739d3a38341efb11a41e5b90be259623b64508 on codex/issue-backlog-completion. engine/render/CameraEdgeScroll.h parses the manifest and tracks dwell independently on each axis. apps/client/GameClient.{h,cpp} and ClientUi.cpp integrate with existing desktop frame input, reset on screen/focus changes and preserve keyboard/gamepad routes. docs/game-format.md and docs/enhanced-input.md describe defaults, units and disabling.

macOS: cmake --build ../issue-backlog-completion-mac --target aiverse_camera_behavior_tests --parallel 2 succeeded. ctest --test-dir ../issue-backlog-completion-mac -R '^camera_behavior$' --output-on-failure passed 1/1. tests/render/camera_behavior.cpp checks default boundary, custom zone/dwell/speed, a fractional activation frame, corner motion and cancellation. Both client translation units passed clang++ -fsyntax-only with hosted-client CMake flags. Existing standalone MCP binary from pr121-completion wrote the three-field manifest block in a disposable project via set_manifest, and game_info in a fresh process read it back. Actual pointer motion in a running client and final merged-tree gate still pending.
