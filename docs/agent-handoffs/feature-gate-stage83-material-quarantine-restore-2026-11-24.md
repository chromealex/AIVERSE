# Stage 83 — material quarantine restore and retry

Parent 4a7cb751, PR #183. Reuse pre-exposure eligibility for material quarantine list/restore; preserve staged bytes. Record style/slot/name/resolution, retry by quarantining previous intent and calling canonical publisher with recorded recipe (current style content, not pinned bytes). Scoped Studio controls for quarantine, restore and retry; archived data preserved. No exact staged material resume, exposed-content rollback, MCP material recovery or execution evidence. Native restore fixture authored NOT RUN. Main untouched.
