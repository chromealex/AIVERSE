# Stage61 — dependency preflight
Branch: feature/world-processes-stage31-bidirectional-handles. Main untouched.
3743a373 adds preflight to examples/world/terraweave_native/Integration.h: require authored species, exactly one previously registered evolve.succession, and no existing terraweave.forest_ecology before any setup mutation. 447d00e0 documents opt-in composition in README.
PHYSIS registration remains caller responsibility; NEREID/terrain order is inherited from existing composition. No standalone demo or production game registration created. No compile or runtime tests. Next: add concrete demo configuration and verify PHYSIS dependency names, then QA when authorized.
