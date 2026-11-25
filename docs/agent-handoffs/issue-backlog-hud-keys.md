# Project HUD shortcuts (#172, #179)

Scope: type and tech hotkeys, command-card display and enabled click dispatch, platform key names for rules commands, and a view-script key handler with text focus protection. Validate as part of combined acceptance.

fc6eebd44d05eaff1868775193f0e6d8c90e1775 changes engine/game/GameData.{h,cpp} and apps/client/{ClientGamepad.h,ClientGamepad.cpp,ClientScriptCmd.cpp,ClientUi.cpp,GameClient.h,GameClient.cpp}. Enabled production actions share click callbacks; display-order selection and key aliases are described in docs/game-format.md. Match keydown payload and consumption are documented in docs/scripting.md. Build, keyboard focus, duplicate-key, match reset and device checks deferred until all requested implementations are in place.
