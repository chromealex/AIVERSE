# Handoff: macOS background runs (`AIVERSE_BACKGROUND=1`), 2026-11-24

- Branch: `feature/aiv1124_mac_background_window` (from `main` 1b77c21bf). Code commit: `b7f68d88d`.
- Area: platforms (`engine/platform/macos`). Invisible tooling for agents / CI; no devlog entry (nothing to show
  on a frame: the point is that no window is shown).

## What changed
- `engine/platform/macos/PlatformMac.mm`: `AIVERSE_BACKGROUND` (set, not empty, not `0`) makes the client
  - use `NSApplicationActivationPolicyAccessory` (no Dock icon, no menu bar, never the active app);
  - create an `NSWindow` subclass that is never key / main and keeps the frame it is given
    (`constrainFrameRect` returns it unchanged);
  - place the window beyond the bottom-left of all screens and `orderBack:` it instead of
    `makeKeyAndOrderFront:` + `activateIgnoringOtherApps:`; window modes (maximized / fullscreen) and
    `applyFullscreen` are skipped; `openURLs` does not activate.
  The window stays ordered in, so `tick` renders and `AIVERSE_SCREENSHOT` captures as before.
  Without the variable the code path is the previous one.
- Docs: env knob in `docs/agents/contracts/build-testing.md`, `docs/game-format.md` and `docs/ru/game-format.md` (§ window).
- Public API / save format: unchanged.

## Verification (macOS 27.2, Apple Silicon, Metal)
- `cmake --build <build> --target aiverse_client` (Release, Makefiles) — exit 0.
- External game project, `AIVERSE_BACKGROUND=1 AIVERSE_SCREENSHOT=… AIVERSE_EXIT_AFTER_FRAMES=400 AiverseGame --map 18 --size 540x960`:
  exit 0, 1080x1920 capture with the map, HUD and cards (Retina 2x).
- While a 900-frame run was going: `CGWindowListCopyWindowInfo` shows the window at X=-4540, Y=5440 (display
  2560x1440 at 0,0); `lsappinfo` shows the app as `type="UIElement"`; polling the frontmost app every 0.5 s
  during runs never showed the client.
- NOT RUN: foreground (no variable) path after the change (unchanged lines, only wrapped in `else`);
  `./run-tests.sh --quick` (no KINGSTON volume on this Mac); other platforms (variable is ignored there).

## Limits / next
- A background window gets no mouse / keyboard input: scripted clicks still need a visible window
  (or a future input-injection hook). Linux runs use Xvfb as before.
- Nothing else pending.
