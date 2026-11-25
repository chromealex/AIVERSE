# PR 194 merge verification

Test repairs in 4c34adccf update tests/feature_gate_test.py and apps/studio-native/tests/StudioAssetTransferTests.cpp. The registry fixture now provides linked-only metadata and an authenticated-link refusal per Studio tool. A successful standalone response fails the audit. The transfer fixture keeps its original journal plan for undo and transfers ownership of the rename plan.

python3 tests/feature_gate_test.py passed 21/21. The repaired asset-transfer fixture compiled and linked using the existing KINGSTON Studio libraries. Running that executable raised “Resource repair history dependencies changed”. Asset-transfer acceptance remains failed. Investigate resource repair replay and verify the full fixture before integration.

On candidate 9804f92d3, strict verification passed for fixed-math-spatial-algorithms, reported performance as not_run for world-processes-planet-authoring and reported seven not_run criteria for studio-document-history. An earlier quick run against a different base does not certify this candidate. Complete current-base verification and resolve review scope before squash.
