# Local match saves and replay viewer (#164)

Acceptance covers save/load after restart, bot continuation, slot listing, replay commands at their recorded ticks, pause/speed/end checksum, bad slots preserving the live match and UI Lua. Combined tests pending.
d55bf8e33d117aa59262c272b02d5d397a4df22d
MatchFiles owns bounded local slots and atomic same-directory publication. Replay encode/decode reuses the existing binary command stream. Runtime host wiring follows. No tests run.
b16515c12800011a99413e1e7aad27a8d762cb71
GameClient rebuilds match setup, validates the composed content hash and snapshot before replacing the simulation, and restores bot state. Replay uses recorded inputs with a read-only command sink and verifies the end checksum. Menu and Lua work follows.
0f055144b512b49a8a9babd8265b06e6ca9a2660
GameClient writes snapshots and native timelines, exposes saved slots in the stock menu and HUD, and registers match_files save/load/record/watch/control for custom UI Lua. Runtime verification deferred.
681622c3d548f0e58c8fad20f605b1f95ffc56e9
Native .rpl imports are matched against installed map paths and the network content hash. UI Lua and storage behavior documented. Client file and imported-replay acceptance remain pending.
e5df9c90bec7e5e7292f43f17ad304e33d362e73
tests/tests.cpp now records a live Simulation, serializes and restores its slot, continues both worlds and plays their command stream back from client and native replay files. Also checks separate save/replay namespaces, metadata, invalid names and truncated reads. Runtime execution pending.
6956228101a129fd5c1481a5964a6c2f45371919
Observed in Chrome: a recording saved mid-match ended at exactly 3:34 with the completion dialog incorrectly reporting Defeat. ClientUi.cpp now distinguishes a recording that ended with result zero from a resolved match and uses the localized matchfiles.finished label (en/ru). Repeat the save/watch/end path in the packaged client.
The full Web package built with aiversebuild. In a fresh Chrome storage origin, saved a two-second unfinished match and its replay, reloaded to the main menu and opened the persisted replay. At 0:02 the stock end dialog showed End of replay with no checksum mismatch. Screenshot: /Volumes/KINGSTON/AIVERSE-builds/issue-backlog-demo/qa/final-web-replay-end.png. The earlier build displayed Defeat for the same case.
