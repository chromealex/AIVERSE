| Intent | Reuse | Required input |
|---|---|---|
| Shop/craft/exchange | offers + buy | cost/reward asset IDs; operation ID |
| IAP | Store + reserveOrder + fulfillOrder | validated player/product/receipt, stored order snapshot |
| Offer targeting | Condition + Schedule | trusted Context; priority/group for display |
| Currency/inventory | transact | trusted bundles, cap, atomic Commit |
| Quest/pass/achievement | tracks + recordEvent + claim | immutable event ID/time, metric, thresholds |
| Login chain | daily + claimDaily | authority time, missed-day policy |
| Stamina | energy + regenerate | call before spending, initial profile balance |
| Inbox | deliver + claimMail | immutable mail ID, expiry, reward |
| Rating | Leaderboard | scope, score policy, rank style, trusted submissions |
| Chat | Chat | channel, auth/moderation callbacks, persistence |
| Segments/experiments | Condition, cohort, resolveSettings | stable experiment ID/weights, trusted values/tags |

Examples: examples/meta/{meta,boards,chats}.json. C++ consumer: examples/meta/native/Main.cpp.
JSON defaults are documented in public structs; Config::parse, BoardConfig::parse and ChatConfig::parse reject unknown keys.

| Extended intent | Reuse | Recipe |
|---|---|---|
| Friends/clans/donations | Service friends/clan actions | skills/meta-clans/SKILL.md |
| Item instances/equipment/merge | Service items actions | skills/meta-items/SKILL.md |
| Entitlements/subscriptions | Service entitlement events | skills/meta-backend/SKILL.md |
| Tournaments/leagues | Service tournament actions | skills/meta-tournaments/SKILL.md |
| HTTP/SQLite/native transport | server.py, Service, RemoteBackend | skills/meta-backend/SKILL.md |
| Reusable meta screens | examples/meta/ui + Service.view | skills/meta-ui/SKILL.md |


LiveOps packages (use `catalog.py describe ID`): loot, albums, jobs, research, quests,
events, wars, async-pvp, matchmaking, ratings, referrals, ads, offer-chains, notifications,
offline. Each routes to `skills/meta-ID/SKILL.md`; services `asyncPvp` and `chains` use
camelCase/short service names explicitly shown by describe. All use the existing Service
and economy, with optional definitions under extensions.liveops.
