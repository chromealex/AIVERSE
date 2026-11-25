# Command card layout (#160)

Acceptance covers fixed coordinates across selections, nested navigation/back, shared hotkeys on distinct pages and legacy sections. Combined runtime checks pending.
7d8ef60271fd9642d16e1aa6ddb558728f36be23
Types, research, unit abilities and scripted commands read cardSlot. ClientUi derives the page tree from available actions, reserves occupied cells before auto placement and builds navigation callbacks. HUD positions buttons by resolved row and column. Runtime checks pending.
e5145f78d7fe985b53b6bf523b6585627dc3ebac
Stock movement and combat actions, rally and select-army accept manifest cell overrides. docs/ui-scripting.md includes two nested pages and legacy HUD behavior. Visual acceptance and tests still pending.
