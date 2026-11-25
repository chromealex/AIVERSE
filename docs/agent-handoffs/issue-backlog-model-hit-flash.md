# Model hit flash (#171)

Delivery scope: type view parser, client and editor GameView, shared GL/Metal model shaders, entity_fx scripting, documentation and example. Verify disabled, default, custom colour, duration and strength on the combined tree after the remaining issue implementations are in place.

Source stage 9f7b23217fe0aa0d4211cd288961f4829a62a6b9. GameLook.cpp parses hitFlash, GameViewDraw.cpp applies event age and the EntityFx.h intensity multiplier, GameViewScript.cpp exposes the field, and Renderer3DModels.cpp writes the packed colour into lod.w consumed by both ModelShaders.h backends. Documentation in docs/game-format.md and docs/scripting.md. No builds, tests or GPU captures run for this change. Continue the remaining issue implementations before combined acceptance.
