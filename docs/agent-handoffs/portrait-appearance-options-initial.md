# Portrait appearance options — #212

Owner: gameplay / universal View
Branch: codex/issue223-minimap-atlas-20261126
Stage commit: 97920cdbc2ac6d022c4f60c0500ff1d48bf8f60e

## Delivered source

engine/view/PortraitOptions.h resolves native presentation settings independently of player/type lookups: project defaults, per-subject overrides, finite/clamped camera controls and RGB/RGBA or host-supplied colour background. ClientUi translates player ownership into colour. GamePortrait forwards the resolved background to both static packet and animated legacy rendering paths. Model source is parsed but is NOT wired to rendering yet.

Paths: engine/view/PortraitOptions.h, engine/view/game/GamePortrait.{h,cpp}, apps/client/ClientUi.cpp, tests/tests.cpp, docs/game-format.md and feature/roadmap policy.

Evidence: git diff --check passed. Added inheritance, player colour, invalid array fallback, explicit empty model and clamp regression cases; tests and builds NOT RUN. User requests one final build after source work. No readiness/issue closure claim.

## Next

Connect separate model source through existing ModelAssetProvider resource owner and generic View/MotionWeave preview packet, retaining team tint, idle playback and configured framing. Do not introduce another animation solver or extend legacy GameView with general rendering logic. Inspect asset reload/shutdown ordering and native test coverage. Verify runtime capture, editor/data surfaces, final Mac gate and ready manifest before merge. Other open work remains #219/#221 and final validation of earlier stages; #206 requires actual Windows runtime evidence.

## Alternate source stage — 3581846fbddeab96012fe784b15494d06a783b82

Added engine/view/ModelPreview.h: one preview subject, ModelAssetProvider asset ownership, View/MotionWeave graph playback (Idle/Stand/first clip), native prepare/camera/submit/reset/shutdown API. No new animation sampler. GamePortrait only supplies its offscreen target; GameClient supplies reader, owner tint and source settings and shuts down provider after portrait rendering. Source failure falls back to the world model. Clip compilation failure is cached and handled. View subject is reset on source changes or hidden selection.

Other paths: apps/client/ClientUi.cpp, GameClient.{h,cpp}, engine/view/game/GamePortrait.{h,cpp}, tests/view/portrait_appearance_api.cpp, docs/view.md, docs/game-format.md, skills/gameplay/aiverse-view/SKILL.md, manifest. Existing authoring operations confirmed in Workspace.cpp: rules_set and set_manifest. Added native signature checks; no tests/build executed. git diff --check passed before commit.

Still required: native runtime with static and animated synthetic alternate sources, background/player colour screenshots, invalid source fallback, source switch/repeated selection/teardown, data persistence through Studio/MCP, final build and gate. Resource invalidation/hot reload of a same-source asset is not currently exposed by ModelPreview; audit whether project lifecycle requires it. #212 is not closed or merged. Next implement #219 and #221 shared render blend capabilities before the final build.
