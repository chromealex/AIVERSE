---
name: meta-ui
description: Reuse AIVERSE meta browser screens and native view projections for shop, quests, leaderboards, chat, community, collection, inbox and tournaments.
---

For selected screens use `tools/meta/kits.py` and `economy.metadata.metaScreens`; the
project guide is `meta/AGENTS.md`. Preserve legacy metadata on existing projects.

1. Generate with tools/meta/scaffold.py --preset full --with-ui --out PROJECT. Read docs/meta-service.md for compose/run commands.
2. Reuse examples/meta/ui (HTML/CSS/JS, no external packages). Configure labels in lang.json and colors/layout through CSS variables. Do not regenerate the whole UI for each game.
3. Bind screens to Service.view and the documented RPC catalog. Treat all text as plain text; keep receipt journals/server authority out of the UI.
4. Preserve operation ID on network retry. Disable mutations while a request is pending and show errors. Do not treat a refresh failure as permission to create a second purchase operation.
5. For native RmlUi consume the same projection through existing project UI components and Localization::tr. Browser assets are not RmlUi documents and are not auto-installed into the engine HUD.
6. Hand real-money offers to existing native Store providers; browser example does not initiate platform payments.
7. Verify desktop/mobile layouts and actual backend actions. State clearly if screenshots, native embedding or network tests could not run; static checks do not establish visual correctness.

Separate client asset library: `examples/meta/ui/` (copied only when UI is selected).
No C++ policy archive is required; native hosts use `AIVERSE::meta_client`.
