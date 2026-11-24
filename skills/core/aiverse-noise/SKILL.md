---
name: aiverse-noise
description: Deterministic periodic value and cellular noise for native C++ projects.
---
Use aiverselib/algorithms/Noise.h. Q16 coordinates and integer lattice periods (zero means nonperiodic). periodicValueNoise<2/3> returns signed Q16; cellularNoise<2/3> returns F1/F2 squared Q32, periodic feature images counted separately. Existing engine/game/gen/GenNoise.h remains authoritative for legacy gradient/FBM/ridge/warp sequences; do not replace saved-world generation. No tests.
