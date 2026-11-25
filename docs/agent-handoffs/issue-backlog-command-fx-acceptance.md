# Command cards, combat presentation and audio acceptance

Tested during the backlog integration from the feature series through cd80cd8c7. The model texture-frame correction at 0e4a94635 has its own acceptance record. Evidence files below are in /Volumes/KINGSTON/AIVERSE-builds/issue-backlog-demo/qa.

- #160: Metal command card retained the root sections and configured column. Orders opened Combat, Combat opened Attack, and Escape twice restored the selected unit's root card. See card-root.png, card-orders.png, card-combat.png, card-return.png.
- #167, #172: the card displayed the configured W build hotkey. At 5 gold, W on the 50-gold watchtower displayed the insufficient-gold notice and kept the original selection. See build-key.png.
- #179: the running native client delivered F9 and Enter to the view Lua keydown handler. Its HUD displayed the received key and its camera action ran. See function-keys.png.
- #143, #147: an authored projectile with explicit +z forward advanced and changed pitch along its homing arc. See projectile-model-115.png and projectile-model-175.png.
- #151: a Lua emitter rendered at the first position, moved with its original handle, disappeared after kill and stayed gone after a stale move. See script-fx-125.png and the corresponding 295 and 470 captures.
- #150, #154: a three-segment chain followed successive targets and showed 6 damage per target at weapon damage 4 with multiplier 1.5. The beam expired. See chain-145.png and chain-310.png.
- #153: the HUD showed normal hit 5, critical hit 10, and +3 GOLD when only 3 of a nominal bounty of 25 fit. See popup-150.png, popup-275.png and popup-reward.png.
- #168, #170: the native mixer maintained one site loop, reversed its stereo balance after the site moved and faded to zero after completion. The six configured interface actions resolved their distinct cue bindings. See construction-audio.cpp and construction-audio.log.
- #152: the renderer logged two skipped combat cues under the configured budget while persistent effects remained active. See budget-acceptance.log.

These focused checks supplement the merged-tree Mac gate. The gate was still running when this record was written.

Additional native Metal runs used the same game host. #146 hit-socket.png places the cyan hit marker at the authored torch node and hit-fallback.png places it back at the model when the requested node is missing. #171 flash-custom-960x640.png shows the guard turning green on a hit; flash-off-960x640.png retains the guard's red color in the same scene. #175 design-aspect-960x640.png and design-aspect-1280x640.png preserve the same left and right map limits while the world stretches horizontally at 4:3 design aspect.
