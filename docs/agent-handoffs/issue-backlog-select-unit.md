# Minimap unit shortcut (#129)

Scope: own type or idle worker, current entity generation and ownership, select or center, flat and planet cameras, optional HUD binding. Verify during combined acceptance.

Implementation d6c02c84880cea006d6ce65347f3195ff7d2721a in apps/client/{ClientUi.cpp,GameClient.h,UiModel.h}, engine/assets/ui/{hud.rml,theme.rcss} and localization files. The action resolves live ownership again at activation and prefers the already selected eligible unit. Combined tests and visual acceptance pending.
