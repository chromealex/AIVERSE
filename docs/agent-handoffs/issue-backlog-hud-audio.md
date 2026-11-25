# HUD interaction audio (#170)

Scope: menu/top/card click, error, map ping and rally acknowledgement. Keep generic click/move compatibility. Combined acceptance must cover missing bindings, disabled buttons, placement failures and minimap/world input.

Stage cc09b0653f268a9e2321eacbac5a109ca75ceaa7 connects engine/audio/GameAudio.{h,cpp} and apps/client/{GameClient.h,GameClient.cpp,ClientUi.cpp}. Match pings use the existing OverlayContext queue. No tests run at this stage. Next verify click classification, configured fallback sounds, rejected placement and exactly one local ping per gesture in the combined run.
