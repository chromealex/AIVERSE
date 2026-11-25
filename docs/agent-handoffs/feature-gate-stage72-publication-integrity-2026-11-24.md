# Stage 72 — publication byte integrity

Parent 25cbad7f, PR #183. Reuse engine pack SHA-256, link editor core to existing pack target. Record staged PNG/manifest hashes before texture publication, record generated building texture hashes before material save. Read-only diagnostics compare exact bytes, reject invalid filenames and bounded file reads; older intents remain unverified. Matching hashes are not ownership proof against journal modification. No automatic deletion, metadata repair or durability guarantee. Known digest and modified bytes fixture authored, NOT RUN. Main untouched.
