# Selection silhouette (#174)

Acceptance: game.json disables both selected and hovered model silhouettes, keeps user overlays visible, and preserves existing behavior when omitted. Verify in client and editor play.

Code fd8e7ee202677531d697e7ee04aae3841e385990 on codex/issue-backlog-completion. GameClient.cpp reads the existing manifest at view startup. Editor.cpp applies the game option while drawing the gameplay viewport and restores the previous GameView setting afterward. GameView.h exposes the prior value. Format and authoring examples live in docs/game-format.md, docs/models.md and skills/gameplay/aiverse-view/SKILL.md. No simulation state or serialized match layout changes.

Mac clang++ with CMake flags from aiverse_hosted_client and aiverse_editor and -fsyntax-only returned 0 on both changed translation units. Existing pr121-completion/mac/aiversemcp --no-editor --engine <worktree> wrote selectionOutline false and true in a disposable project through set_manifest. For each value a fresh process reopened the project and returned the expected boolean through game_info, matching game.json on disk. This checks existing MCP authoring capability only. Visual captures in game and editor, final combined-tree quick gate, and integration remain pending.
