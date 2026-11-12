---
name: meta-database
description: Set up or migrate server-only AIVERSE meta storage using the SQLite reference schema or an atomic CAS provider; keep DB state and session hashes off clients.
---

- Reference schema: `tools/meta/db/001_initial.sql`; migration owner: `tools/meta/server.py`.
  `snapshot` stores the complete server aggregate; `sessions` stores token hashes and trusted
  player/authority/expiry. `meta_schema` tracks applied migrations. No raw bearer tokens in DB.
- Initialization upgrades legacy reference DBs in one transaction without replacing snapshots
  or sessions. Reject a newer schema; never reset it or erase profiles to make startup succeed.
- The reference HTTP host serializes operations with BEGIN IMMEDIATE and commits state plus
  receipts before responding. An external provider must preserve revision/CAS semantics and
  cross-profile atomicity, not split wallet changes and dedupe records into separate commits.
- SQL, migrations, database paths/backups and private state belong to the server distribution.
  Client projects receive only protocol/public views and their per-account pending outbox.
- Back up before a destructive schema change; append a numbered migration and test upgrade,
  rollback-on-failure, repeat initialization, conflict and restart. Do not edit applied schemas
  to silently reinterpret existing data. Dedicated DB access belongs to authenticated server code.
