# Skill audit and acceptance replay

The full Mac build passed at `fb77c8bf1`. During its 534-case CTest run, `skill_audit` flagged eight obsolete source or command references. Commit `1748b1027` corrects the Caverns and pathfinding skills' retired ActorHost reference and identifies planetary edits as modes of the registered native operation. `python3.12 tools/skills/audit.py --strict` then reported zero errors and zero warnings.

All nine prior acceptance commands were re-executed on the corrected source branch. The combined report is `docs/evidence/integration-acceptance-1748b1027.json`; its captured log includes the complete Studio, graph, native Building, MCP, Caverns save/reopen and GLB export results. The live Studio project fixture is copied to a temporary directory before its descriptor is created. The manifests retain their individual evidence references and use the combined report for checkout freshness.

While this run was in progress, origin/main advanced to `0e7e2ddd9` (SpectraFX). A three-way preview found 24 conflicts. Both native preview owners and both sets of guarded graph tests must survive reconciliation before rerunning the gate.
