# 103 — Publish: Build panel

Branch `feature/studio-html5` (after merging main at 4d2c79ecd: MotionWeave / VIVID digital humans / Studio
facial — conflicts resolved by keeping both sides: `rules` + `human_appearance` panels, selection flags +
facial preview, `sssThickness` + `humanProfile`/`eyeOptics` in material hashes, floating-window scene checks +
facial GPU status, main's pose test).

## Done
- **Build panel** (`panels/BuildPanel.tsx`, Publish workspace centre, also Window → Build). Packages the
  open game with `aiversebuild` through the Workspace build runner (the same one as the legacy Build window
  and MCP `build_start`):
  - game identity (name, id, version, build, orientation), platform list (macOS, iOS, Android, Web,
    Windows, Linux) with a *built* badge, Release/Debug, *Install missing tools first*;
  - toolchain of the selected platform (`--check-toolchain --json`): found / missing / installable;
  - **Build**, **Estimate size** (content pack only, `--estimate` into `Builds/.temp/estimate`: pack size,
    raw size, file count, heaviest files), **Show build** / **Show log file** in Finder;
  - live status: current `==> stage`, elapsed time, exit code, error lines, *Missing tools → Install and
    build* (`AIVERSE_MISSING_TOOLCHAIN`), streamed log (polled by UTF-8 byte offset, `next`).
  - A project with unsaved changes is never built (no implicit save): *Save and continue*.
- Native: `studio.buildInfo|buildToolchain|buildStart|buildLog|buildEstimate` in `StudioSession`
  (`buildRequest`), `studio.buildReveal` in the host (path from the session, never from JS). Studio builds
  and depends on the `aiversebuild` target (`AIVERSE_STUDIO_BUILD_TOOL`), fallback `findBuildTool`.
- Saved Publish layouts gain the Build tab once (`WORKSPACE_ADDED`); `build` left the legacy list.
- Tests: session (targets, unknown target/config, dirty project refused, no estimate yet, log offsets),
  web `buildLogic.test.mjs`, nested-dock defaults.

## Verified on the Mac
- Rebuild PASS (web, native, session tests, RTS pose test).
- On a private copy of `tests/checksums/games/lua_lab` (`build/claude/studio-buildtest.command`, KINGSTON):
  toolchain table for macOS, Estimate size streamed its log and finished: 141 MB content pack,
  heaviest files listed (engine vegetation textures, stored uncompressed).
- Bug found and fixed while verifying: the log offset was the JS string length, not UTF-8 bytes; the Russian
  log then split a character, the reply was invalid UTF-8 and the panel stalled.

## Not done / next
- A full platform build was not run (long); Branding and the asset audit are still legacy-only.
- Finding: the lua_lab pack carries ~140 MB of engine vegetation PBR textures it does not use.
