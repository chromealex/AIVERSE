# W1.20d — checked adaptive job journal integration

Commits `fb8b63b35e2004459b4860c2244f27db3463d655`, `378496a195d8f235bc0fb319f8f8d980bc2caf4a`. PR #116 Draft.

`engine/core/ECS.h`: serialize journal record and merge with a static mutex; registration, drain, copy and clear require quiescence. `engine/experiments/EcsGuardedWorld.h`: eachAdaptiveWrite collects driver handles in a range-local batch and publishes on range completion, including exceptional exit. Secondary GuardedWorld::write accesses use synchronized World::tryGet tracking. Direct raw pool writes remain outside coverage. This is the checked experimental scheduler only, not all engine Jobs. No native tests run. Next: avoid collecting batches for untracked types, audit remaining writers, integrate actor tracking with full scan fallback and restore bootstrap; update skill/catalog. No main merge.
