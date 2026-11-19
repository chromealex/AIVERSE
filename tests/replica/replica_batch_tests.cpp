// Regression: Place's stack-backed updates must be indistinguishable from
// legacy per-field Server::set, including quantization, reliable fields,
// duplicate updates, change versions, and transmitted snapshot payloads.
#include <array>
#include <cmath>
#include "tests/replica/ReplicaTest.h"

namespace rt {

TEST(batched_fields_preserve_legacy_replica_wire_and_values) {
    ServerOptions options;
    options.defaultRadius = 5000;
    options.bytesPerSecond = 1 << 22;
    options.maxSnapshotBytes = 50000;
    LoopWorld legacy(options), batch(options);
    auto& lc = legacy.add();
    auto& bc = batch.add();
    legacy.server.setViewers(1, {{0, 0, 0}});
    batch.server.setViewer(1, {0, 0, 0});
    ObjectId lp = legacy.server.create(legacy.game.player);
    ObjectId bp = batch.server.create(batch.game.player);
    ObjectId lb = legacy.server.create(legacy.game.prop);
    ObjectId bb = batch.server.create(batch.game.prop);
    CHECK(lp == bp && lb == bb);

    for (int i = 0; i < 240; ++i) {
        const float x = float(i % 91) * 0.1f;
        const Value position = vec(x, 0.5f, -x);
        const Value rotation = quatYaw(x * 0.03f);
        const Value asleep = ival(i % 7 == 0);
        const Value health = ival(i % 101);
        const Value label = sval("player" + std::to_string(i / 29));

        legacy.server.set(lb, legacy.game.bPos, position);
        legacy.server.set(lb, legacy.game.bRot, rotation);
        legacy.server.set(lb, legacy.game.bAwake, asleep);
        const FieldUpdate bodyFields[] = {
            {batch.game.bPos, position},
            {batch.game.bRot, rotation},
            {batch.game.bAwake, asleep},
        };
        batch.server.setFields(bb, bodyFields);

        legacy.server.set(lp, legacy.game.pPos, position);
        legacy.server.set(lp, legacy.game.pRot, rotation);
        legacy.server.set(lp, legacy.game.pHp, health);
        legacy.server.set(lp, legacy.game.pName, label);
        const FieldUpdate playerFields[] = {
            {batch.game.pPos, position},
            {batch.game.pRot, rotation},
            {batch.game.pHp, health},
            {batch.game.pName, label},
        };
        batch.server.setFields(bp, playerFields);

        // The newer update wins in exactly the original order, including
        // an intentionally invalid index and an empty update span.
        if (i % 23 == 0) {
            const Value newer = vec(-x, 1, x);
            legacy.server.set(lb, legacy.game.bPos, newer);
            const FieldUpdate extra[] = {
                {-1, position}, {batch.game.bPos, newer},
                {500, rotation}
            };
            batch.server.setFields(bb, extra);
            batch.server.setFields(bb, {});
        }
        const float viewX = float(i % 39) * 0.25f;
        const std::array<float, 3> viewer{viewX, 0, -viewX};
        legacy.server.setViewers(1, {viewer});
        batch.server.setViewer(1, viewer);
        legacy.frame();
        batch.frame();

        for (int f = 0; f < 3; ++f)
            CHECK(legacy.server.get(lb, f) == batch.server.get(bb, f));
        for (int f = 0; f < 4; ++f)
            CHECK(legacy.server.get(lp, f) == batch.server.get(bp, f));
        CHECK(legacy.server.stats().snapshotBytes ==
              batch.server.stats().snapshotBytes);
        CHECK(legacy.server.stats().entries == batch.server.stats().entries);
        CHECK(legacy.server.stats().deltaFields ==
              batch.server.stats().deltaFields);
        CHECK(legacy.server.stats().evaluated ==
              batch.server.stats().evaluated);
    }

    legacy.run(40);
    batch.run(40);
    CHECK(lc.ready() && bc.ready());
    CHECK(sameState(legacy, lc, lp) && sameState(batch, bc, bp));
    CHECK(sameState(legacy, lc, lb) && sameState(batch, bc, bb));
    CHECK(legacy.server.stats().snapshotBytes ==
          batch.server.stats().snapshotBytes);
    CHECK(legacy.server.stats().reliableBytes ==
          batch.server.stats().reliableBytes);
}

} // namespace rt
