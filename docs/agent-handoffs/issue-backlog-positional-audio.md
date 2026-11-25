# Positional voice lifetime (#168)

Scope: script voice play, move and fade, per-type construction loop, visibility, completion, destruction and detach. Verify on the combined tree.

Implementation af3b023af8becbe7479c67ed0895f7a2ca86e4ae and lifecycle follow-up 2fc7c55b9187487f8a3ba1dcea0e8df2f4203496. Files: engine/view/game/ScriptSound.{h,cpp}, engine/audio/{AudioScriptSound.h,AudioScriptSound.cpp,GameAudio.h,GameAudio.cpp}, docs/{game-format.md,scripting.md}. Reconcile voices by entity generation and actual mixer state, retain match event subscription on bank reload and avoid sending redundant position commands for stationary sites. Tests and live playback deferred to combined acceptance.
