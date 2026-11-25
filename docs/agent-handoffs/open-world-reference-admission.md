# OW-01 component reference admission

Base 8511f3094. Acceptance requires native GameCore queue rejection, stable-handle internal links, retry after release, and fresh save/load. Host-defined references must be read from the authoritative component at the transition barrier. Existing unguarded hosts retain their behavior.

Code 8dfcd4503. The guarded path in engine/world/RegionLifecycleRuntime.h accepts enumerators of live Entity references, including collections. Both directions of crossing links block the whole group. No new persisted data, editor setting or MCP operation. Host policies must still cover semantic IDs, opaque references and resource-owned claims. The setup example is in skills/world/aiverse-region-lifecycle/SKILL.md.

On Mac/KINGSTON the aiverse_region_gamecore_integration_test build and ctest -R '^region_gamecore_integration_test$' passed (2.32 seconds). tests/world/region_gamecore_integration_test.cpp exercises incoming and outgoing links, internal links, atomic parking, release/retry and fresh restore of pending and parked states. ActorHost parking of live character/vehicle runtime components has separate simulation-ordering and ownership requirements. Next: exact merged-tree Mac quick.
