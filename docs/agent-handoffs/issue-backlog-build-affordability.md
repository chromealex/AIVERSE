# Build placement feedback (#167)

Accept immediate localized notice and resource-specific sound on insufficient upfront funds, generic sound fallback, no ghost or command on rejection, and existing progressive, delivered-goods and prepaid placement behavior.

Code 0a80c21d0fb72a6e47a2e37ca24c24318b6b3553 on codex/issue-backlog-completion. GameCore::canAffordBuildStart queries canAfford and upfrontCost at click time. ClientUi.cpp enters placement only after success and leaves prepaid producer placement separate. GameClient.cpp shares the translated resource shortage notice with confirmed simulation failures. GameAudio.cpp shares per-resource sound selection and generic fallback between those two paths. No synthetic simulation event, command or debit on rejection.

tests/tests.cpp registers build_button_checks_only_immediate_cost and build_button_missing_resource_audio. On macOS x86_64, extracted the latter test body, compiled GameAudio.cpp and GameData.cpp against the current audio/engine archives, and ran ../issue-backlog-completion-mac/build-feedback/test: exit 0. The test uses an offline audio device and per-sound cooldowns to prove which sound played. CMake target flags with clang++ -fsyntax-only passed for GameClient.cpp, ClientUi.cpp and GameCore.cpp. The four-case game-economy fixture, interactive HUD input and final combined-tree gate still require execution.

Ran registered build_button_checks_only_immediate_cost body from the branch on macOS x86_64 with the real Simulation and GameCore. All four stock/delivery/progressive payment cases and checksum assertions passed (../issue-backlog-completion-mac/build-feedback/accept, exit 0).
