# Repeated unit selection (#169)

Accept per-type repeat count, time window and sound variants. Count clicks on the same entity separately, distinguish different entities of the same type, clear sequences on deselection and preserve behavior for projects without the mapping.

Implementation 54d969f7f135df1a8612d922ceba86986ccc5629. GameAudio.{h,cpp} maps types.<id>.repeatSelect, keeps at most 64 presentation timestamps for one generation-safe entity and uses the existing sound bank variant picker. Explicit mouse/touch selection in GameClient.{h,cpp} acknowledges a configured click once, even when the selected vector stays unchanged. Other selection changes break the sequence. docs/game-format.md documents the data.

macOS x86_64: extracted tests/tests.cpp audio_repeated_selection_uses_entity_and_time_window, linked with branch GameAudio.cpp, game and audio archives and ran ../issue-backlog-completion-mac/build-feedback/repeat: PASS #169 (exit 0). Covers identity change, click threshold, timeout, explicit reset and unconfigured fallback. clang++ -fsyntax-only on GameClient.cpp passed. Manual in-client mouse/touch playback and final integration remain pending.

Other backlog verification in the same worktree: compiled and executed tests/libraries/systems_engine_tests.cpp against the current game archive plus directly compiled GameCore.cpp. build-feedback/systems returned 0 with "Engine stats/statuses, commands, sim RNG and restoration: OK". This exercises registered source scenarios for weapon damage scaling, multishot, attack timing, fractional cadence, modifier groups and event queueing. Host-specific integrations and platform acceptance are separate.
