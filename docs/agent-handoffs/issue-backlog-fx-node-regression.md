# Model-node effect placement (#146)

Stage commit 2b3cf836e0f9f2abce5b2a50b65be8e0b7592642 adds fx_node_binding_selects_entity_and_falls_back_to_terrain in tests/tests.cpp.

Built aiverse_game_fx in ../issue-backlog-completion-mac (exit 0). Compiled the registered test body with clang++ for x86_64 macOS against the built GameFx, CPU ParticleSystem, GameCore and engine libraries. Ran ../issue-backlog-completion-mac/build-feedback/fxnode, which printed PASS #146 (exit 0). It dispatches attack, ability and hit in a simulation tick and checks three resulting particle positions for a caster node plus local offset, a target node, and an unknown node with terrain height plus binding height. The test injects node frames through GameFx::setNodeFn.

Next verify real animated-model node transforms through GameView, live authoring and a rendered capture, then run the Mac quick gate on the integration tree. Issue remains open.
