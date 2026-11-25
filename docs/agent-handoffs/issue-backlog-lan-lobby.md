# LAN hosting and discovery (#163)

Acceptance covers a host and separate browser client, compatible game filtering, joining the regular lobby, stale discovery expiry, disconnect cleanup and direct-address compatibility. Combined runtime tests pending.
34749c7f2107e9a99dd66f25010ccf8882347935
LanDiscovery uses a project/protocol-scoped query and unicast reply, derives relay addresses from received source IPs and expires unrefreshed hosts. Socket gains opt-in broadcast. Lobby integration follows. No runtime tests run.
6c6a6b75ff4d380eea327201c7bdfb7fa42c345f
Stock native lobby starts the same Server relay used by aiverse_server, updates it during gameplay and offers discovered addresses through the existing NetClient connection flow. Browser UI retains direct network access. Two-machine verification pending.
f6a465c17879f3527e51c502dc7e3da773f7d8e5
Added a loopback UDP fixture for discovery, project filtering and expiry, usage strings in Apple app plists, and native host/join instructions. The test has not run. Broadcast and two-device match acceptance still pending.
5562420dd6f3ac14dec194dfec3b392f8e629fd4
Mac compiler rejected two calls to the lobby action missing its integer argument. Host loopback and discovered host rows now use the same no-selection value as UI dispatch. Runtime verification pending.
a420adfbdff842f5d1814bcb78c0cb1a1aefae60
tests/net_tests.cpp now starts a real server, advertises its bound relay port, connects one peer to the discovered endpoint, and retains the WebSocket peer and existing room, readiness, match progress and desync assertions. Uses real loopback sockets. Physical network broadcast verification remains separate.
11d09562eb404920a5deae9959b26b114a43bb61
Two independent non-root Linux containers shared an internal IPv4 bridge without a default route. Before the fix, the default query returned ENETUNREACH (101); the host received zero datagrams. Explicit subnet broadcast and unicast each delivered a 96-byte query and 43-byte reply. After the Socket.cpp and LanDiscovery.cpp fix, the original unmodified browser using browse(game) discovered the advertised project, host label and relay port in three independent runs. No published host ports, loopback target or gateway route were used. Mac syntax compilation also passed. Earlier Mac UDP/WS match evidence covers joining the discovered relay; a physical Mac-to-Mac broadcast run remains unverified.
