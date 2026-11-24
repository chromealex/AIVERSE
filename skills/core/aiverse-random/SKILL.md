---
name: aiverse-random
description: Portable deterministic random streams and integer sampling for native C++ projects.
---
Use aiverselib/algorithms/Random.h, namespace aiverse::lib::algorithms. Pcg32 has explicit seed and 63-bit stream selector, serializable state and odd increment, next/next64, unbiased below and advance. deriveSeed is a noncryptographic keyed mixer. randomShuffle and weightedRandom use integer sampling. Preserve existing simulation RNG and save compatibility; persist state explicitly if used in rollback. No tests yet.
