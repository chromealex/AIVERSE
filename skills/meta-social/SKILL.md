---
name: meta-social
description: Add configurable AIVERSE leaderboards and chat channels using the reusable C++ social policies and explicit backend boundaries.
---

- Read `engine/meta/Social.h` and rating/chat sections of `docs/meta-game.md`.
- Pick scope, ascending, update best/latest/sum, rank ordinal/competition/dense, season schedule and limits. Use page/around; account for revision changes across pages.
- Submit verified match results with stable operation IDs. Do not trust a client score. For large boards put sorted indexes in the backend; the local implementation sorts its in-memory scope.
- Create a Chat per channel. Supply authorization for read/send/delete/report and optional content moderation. Bind actor/viewer to authenticated identity in the host.
- Configure allowed kinds, maxBytes, cooldown, history, retention and dedupe capacity. Use history's exclusive before cursor. Render plain text; never inject message contents as markup.
- Persist complete snapshots including receipts/read cursors/rate limits; mutate a copy and publish only after atomic storage success.
- Use a Backend adapter for remote services. OfflineBackend returns Unsupported; it does not simulate global players or connectivity.
- Validate board/chat definitions and exercise authorization failures, retries, season reset, equal scores, blocked users, mute, deleted messages and restored state.
