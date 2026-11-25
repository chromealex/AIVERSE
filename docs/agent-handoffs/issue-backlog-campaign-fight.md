# Campaign first-person encounters (#125)

Acceptance requires entering from a campaign unit, look above and below the horizon, stance and movement, guard sight and sound, capture and retreat, persistent part wounds and effects on movement and view. Check overhead restoration and unchanged existing possession controls. Runtime checks deferred until all implementations are ready.
f646cff787815cc6f8f1ad6ac38256e96f0cf105
GameData enables possession.firstPerson. GameClient and ClientActions attach CameraLook to the fighter, handle relative mouse motion, crouch movement and eye height, and restore the overhead camera. Tests deferred. Next implement campaign outcomes, wounds and guard sensing.
404b4512707e0177080dbf9a32e433ed03ed835b
CampaignFight.h stores registered per-entity senses and wound state. Dungeon.cpp advances policies on simulation ticks, uses movement and shot noise, emits capture on close alert and retreat on release. GameCore, Components and Script expose result and wound events. ClientActions sends quantized aim and stance. Runtime acceptance pending.
032831e6e684342d07cbcfd57a40305d1f76b67a
tests/tests.cpp covers wound penalty, part, save/load and retreat. docs/camera-behaviors.md describes authoring and campaign events. First merged Mac gate started on 51d3cc3ae; current changes require a final rerun.
4be38ca6110ad4a28e6e95a4e750f5078968f152
Combat.cpp previously allowed ordinary RTS acquisition and fire to bypass encounter visibility. Both now check the per-guard awareness record owned by Dungeon.cpp. Existing possessed shooters keep direct control. Integrated checks pending.
fdd2aed0253043b498764e461c80e8d00fbce6d3
CampaignFight persists movement origin and the pending weapon noise. Combat sets noise at accepted attack release; Dungeon samples actual movement. tests/tests.cpp adds sensor-gated acquisition, capture and return using opposing campaign entities. Test execution pending.
9ccb67a2d0d6e6789f3d272d7c2f6c7b8bd18192
demos/campaign-encounter uses the stock HUD and built-in geometry. It lets the player select two scouts or an outpost, enter first person, crouch, retreat and transfer a captured fighter to its guard through campaign Lua. Runtime capture and web packaging pending.
242f3d20fbd5952e2b84d8ba592b4ceb179ea42a
GameClient keeps locked pointer buttons out of the RmlUi element beneath the stationary cursor. ClientActions can acquire browser pointer lock on a world click, and host shutdown releases it. Input scenario verification pending.
85d50c2a7affd779630fc7bf86edeb66451b649d
GameClient uses platform MouseWheel and consumes wheel zoom during pointer lock. Recompile pending.
77cbd5e0e02b91e5f7d2548e5da7f8aa53b133ec
Emscripten 7.0.1 installed on KINGSTON with Python 3.12. First web packaging attempt rejected a hyphen in the application identifier. Changed demos/campaign-encounter/game.json to org.aiverse.demos.campaign_encounter and restarted packaging.
7c678a3a249be236cddc419e1e1f6c71e520c8b2
Headless acceptance found immediate Patrol victory at tick 1 because the other team was authored as a wave player. Removed the exemption and used the scripted AI policy to retain ordinary combat without the generic strategic bot. Recheck save/restore, then repackage browser content.
