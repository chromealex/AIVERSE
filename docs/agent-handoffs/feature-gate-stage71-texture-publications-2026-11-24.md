# Stage 71 — Texture Graph publication intents

Parent 9303ebde, PR #183. Common CPU/GPU image and set publishers record project-local intents outside content, expose read-only texture_graph_publications_get to MCP and Studio. Distinguishes pending/active from destination present but unverified. No automatic rollback, stale lock removal, byte verification, GUID repair or power-loss durability. Tests authored NOT RUN. Main untouched.
