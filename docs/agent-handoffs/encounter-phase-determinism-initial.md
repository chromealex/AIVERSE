# Boss phases and match determinism

The planned scope and pre-fix evidence are in game-checksum-baseline-2026-11-25.md. Implementation commit 690649721a02617cb34a0e64752caceac79c6930 keeps the boss identity across entity creation and reacquires its mutable state. The native regression exercises the spawn tick under both supported component storage modes. No schema or save migration is needed. Next: independently compare all sample matches, verify restore across the phase, record the integrated test checksums and run the full Mac quick gate.
