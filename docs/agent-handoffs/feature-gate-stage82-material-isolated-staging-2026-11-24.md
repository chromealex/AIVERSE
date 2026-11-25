# Stage 82 — isolated building material staging

Parent 6b42be87, PR #183. New building material v2 intents stage generated PNGs outside content; record exposure before atomic directory rename, then catalog registration and material commit. Preserve pending evidence on failures instead of deleting possibly referenced/committed assets. Internal quarantine primitive moves only pre-exposure v2 journal to cache quarantine under writer lease and refuses existing material/content. Tests authored NOT RUN. Quarantine list/restore/UI/MCP and exposed-content rollback remain pending. Main untouched.
