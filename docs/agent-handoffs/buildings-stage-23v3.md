# Building runtime stage 23V.3 — ownership and lift checkpoint foundation

Parent: 23V.2 / eeeff4fedafdb77e0e96a192292d1dcc6d15aed5. Branch: feature/aiv20261123_building-grammar.

- Queued aperture commands now retain the authenticated player slot and are reauthorized both on enqueue and before starting physical motion. Existing callers can omit the guard; the common world registry always installs it, including after recovery.
- Portable lift controllers now capture and restore a committed checkpoint including cabin position, passenger count, door phase/timer, revision and integer travel remainder. Unconsumed physics motion is rejected. Host validates the complete rider map and controller set before changing anything.
- Native circulation scene accepts a restored checkpoint before publishing platform colliders, rejects duplicate/occupied IDs. ECS circulation verifies passenger bindings before capture and restore; detach can preserve restored markers. Persistence adapters use the existing Simulation component/resource registry.
- Regression added to the existing standalone structure-graph executable for occupied lift recovery mid-travel. Fixed portable signed integer min overload.

**Limitations / next step:** BuildingWorldRuntime still rejects elevator recipes explicitly. Compose the restored circulation owner, its collider-ID ranges, regional/external graph ACKs, authenticated lift command dispatcher, and renderer platform/landing snapshots into the common registry before declaring elevator support complete. Existing lift-specific host APIs remain available. Real game module adoption and native build/GPU/network integration validation remain pending. No compilation/test result is claimed for this commit.

Use the existing building skill and 23V.1/23V.2 handoffs for the common runtime contracts. No legacy FPS/RTS ownership, separate physics world or network transport is introduced.
