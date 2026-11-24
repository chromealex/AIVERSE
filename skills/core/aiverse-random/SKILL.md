---
name: aiverse-random
description: Portable deterministic random streams and integer sampling for native C++ projects.
---
Use aiverselib/algorithms/Random.h, namespace aiverse::lib::algorithms. Pcg32 has explicit seed and 63-bit stream selector, serializable state and odd increment, next/next64, unbiased below and advance. deriveSeed is a noncryptographic keyed mixer. randomShuffle and weightedRandom use integer sampling. Preserve existing simulation RNG and save compatibility; persist state explicitly if used in rollback. No tests yet.

RandomSampling.h adds Xoroshiro128Plus (24/16/37 variant), IntegerAliasTable (exact integer weights, transactional build, O(1) expected sampling), reservoirIndices (uniform subset), approximateNormalQ16 (12 uniforms, bounded tails, not exact Gaussian). No tests.

RandomSequences.h: Q32 radical inverse, Halton2, Hammersley2, Sobol2 and jittered stratified samples. Exponential, Poisson (lambda <=64, bounded budget), Gaussian Box-Muller use floating math and are not lockstep-safe. No tests.
