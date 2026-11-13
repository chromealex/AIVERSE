# Independent forward test of original skills

Entry: `skills/aiverse-orchestrator/SKILL.md`. No engine/skill edits were made during this test.

1. `search "магазин чат лидерборд" --domain meta` correctly selects meta-economy and meta-social.
2. `plan --skills meta-economy,meta-social --side both` omits meta-client, CLIENT CMake and client read path. Followed meta-server's explicit link to meta-client manually.
3. `meta-client` points to examples/meta/native/Main.cpp, which is a standalone in-process executable with full legacy Service; it is not a game/native host integration and does not demonstrate separate server transport.
4. The recipes omit where a Client lives and is pumped in AiverseGame. NativeModule simulation callbacks are explicitly unsuitable. The existing NativePlugin init/update lifecycle works; parent investigator identified it. Read engine/platform/Plugins.h, Plugins.cpp and PlatformCommon.cpp to establish the frame/lifetime contract. The project adds host/MetaPlugin.cpp directly to aiverse_client. No engine source changes or renamed entrypoint are needed.
5. docs/meta-game.md and docs/native-modules.md still recommend the no-side helper / aiverse::meta umbrella. Followed newer skill side separation instead.
6. Reference tools/meta/service_main.cpp uses legacy all-policy Service, so selected economy/social server requires project-owned Main.cpp using the generated kit factory. Read reference server.py to preserve its trusted subprocess envelope and transactional SQLite host behavior.
7. No ready HTTP Backend transport is supplied by meta-client. Project implements bounded loopback HTTP on a worker, delivering callback/pump only on main game thread. This is local macOS verification, not production TLS/mobile transport.
8. No documented automatic RmlUi binding is supplied. Runtime plugin acceptance is real client integration but not a complete interactive store/chat/ranking UX.
9. Existing CMake copies the entire content-root. Keeping server configuration/SQL/sessions in the project root without staging would ship them. A whitelist staging root is used under Builds/.temp/public-content; only template game content is copied. Source root includes project/server code but is not the content-root.
10. All builds/temp logs live under Builds/.temp. Ninja is not installed here; use Unix Makefiles with AppleClang. Loopback bind and GUI access need execution outside the sandbox; this is an environment limitation, not a missing policy.

Additional reads: engine/meta/{Meta.h,Social.h,Protocol.h,Client.h,CMakeLists.txt,ServiceSocial.cpp}; selected economy/chat/ranking sections of docs/meta-game.md; docs/native-modules.md native host section; tools/meta/{service_main.cpp,server.py,validate.cpp}; examples/meta/{boards.json,chats.json,service.json}; root CMakeLists.txt (content copying and target wiring); apps/client/main.cpp and GameClient.h (host/lifecycle investigation). Read only selected skills plus explicit links meta-client, meta-database, meta-compose; did not load whole catalogs/schemas.

## Retest after parent fixes

Applied /private/tmp/aiverse-forwardtest-recipe-fixes.patch only after recording the original failures. Updated side=both now includes meta-client and both sides' CMake. Updated meta-client names NativePlugin host lifecycle and explicitly distinguishes automatic probes from UI. Updated meta-server provides aiverse_add_meta_service; project removed its copied Main.cpp and rebuilt the selected helper explicitly. The same real packed AiverseGame passed against that helper, and SQLite restart verification passed. Catalog check and catalog_test passed. Remaining project work is intentional: local async HTTP transport, account/token provisioning, public-content staging, and actual UI if requested. No automatic native RmlUi screens or production transport is supplied by the recipes.
