# S6f.4a — coordinated existing-document save (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 1cacb56cff2ce70e224834e24126f1a61e1a24b8. Detached project save 43a549ed05850c1a4316fb93ebdc1f89bb2f1a1b; map staging aeeb1c65a853b536710587f68bb27b043128f5a9; native integration 47d31aa0e0b11ff4c76446d30ee97f6e2a6b0380; fixture a6688656ed97736e79abb1a45062a9990aeae3f5.

- Native studio.save with dirty Rules/Manifest prepares detached map and project owners. Existing map and emitted terrain sidecars, project documents and Project history checkpoint enter one existing-file journal. Live owners swap after successful commit. Rules-source/module changes concurrent with dirty Rules and new map/sidecar outputs refuse before publication.
- Added interruption fixture for map and manifest publication boundaries and exact rollback. Map history checkpoint retains its post-commit dirty-marker fence; not yet atomically published. Project checkpoint initialization may create an empty baseline before publication. Map staging reads current disk bytes, not a retained exact loaded baseline.
- No change to Save As, standalone map_save or asset cursor checkpoint publication. Existing map-only save remains legacy. No new MCP action.

Evidence: source and test fixture committed, NOT BUILT OR RUN. No native, frontend, CTest or actual interruption execution evidence. No main merge. Next S6f.4b: durable creation-aware journal for new outputs, unified owner save paths, exact loaded-map baseline, map/asset cursor checkpoint staging and crash verification; then S6h.3 and S6i.3. Review cross-owner save path before production use.
