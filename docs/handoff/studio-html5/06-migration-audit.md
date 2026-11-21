# AIVERSE Studio HTML5 — Stage 6: measurable ImGui migration

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 6c7e43799b9c13365fdd7bff988ef860ae0ee18b
Stage commit: see branch history.

## Implemented
- tools/studio/migration_audit.py scans the actual editor source tree for Dear ImGui calls/types and counts legacy window files.
- It also inventories new React components and rejects accidental ImGui dependencies in new native Studio sources (--strict).
- Machine-readable --json output for future agent pipelines.
- Standard-library unittest fixtures added for counts, boundaries and empty trees.
- No game client or genre module is touched. Migration is strictly about the editor UI.

## How to use
python3 tools/studio/migration_audit.py --json
python3 tools/studio/migration_audit.py --strict
python3 -m unittest discover -s tools/studio -p 'test_*.py' -v

## Tests performed
Source committed and GitHub commit verified. The scanner was NOT run against a full checked-out AIVERSE repository here; the connector runtime has no local checkout.
Python unittest suite is written but NOT RUN in the connected repo environment.
CMake, TS, native app, GPU validation remain NOT RUN.

## Files
tools/studio/migration_audit.py
tools/studio/test_migration_audit.py
docs/handoff/studio-html5/06-migration-audit.md

## Interpretation
The reported legacy file counts are not feature-parity metrics.
Do not delete legacy editor code because a static scanner reports fewer calls.
The game client's ImGui overlay is a separate concern, not part of this migration.

## Next
1. Run npm typecheck/build and macOS CMake/CTest smoke tests.
2. Integrate actual native Scene and Game render targets with HTML5 docking, z-order and input.
3. Migrate each editor window and record feature parity, visual screenshots and API coverage.
4. Only after complete parity, remove ImGui from apps/editor and update build definitions.
5. Continue handoff docs after every stage.
