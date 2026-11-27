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
