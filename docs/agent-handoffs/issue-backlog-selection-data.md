# Structured selection (#157)

Scope: current effective unit values, per-selection rows, statuses and select-one callbacks, presentation-only Lua access. Verify group and solo selections, ownership, visibility and stale entities in combined acceptance.

Source a1e8ae83f9727601b2a7910a71cc4c9b4561d0f8 and shared visibility/weapon owner follow-up d269ab5e1bd475fb5d5dc5d4a998b4ccc1c3c3b8. apps/client/{UiModel.h,ClientUi.cpp,GameClient.cpp,GameClient.h} expose structured fields, generation-checked selection actions and solo statuses. engine/view/game/GameViewScript.{h,cpp} exposes view.selection. Weapon values report pre-target primary damage; the data model's fixed primary damage gives equal min and max. Tests and visual acceptance deferred per requested order.
