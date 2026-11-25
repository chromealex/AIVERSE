# Stage 76 — explicit Studio metadata recovery

Parent 62dd0c99, PR #183. Add scoped studio.materialPublicationRepair, UI action only on matching committed material and hash-backed missing sidecar evidence, native revalidates under project writer lock. Catalog refuses assigning replacement GUID for recoverable recorded files. Refresh Assets after all repairs; no automatic broad catalog mutation after repair. No external writer guarantees, MCP repair, journal retirement, quarantine or rollback. Build/tests NOT RUN. Main untouched.
