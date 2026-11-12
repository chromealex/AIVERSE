---
name: meta-server
description: Implement the server side of AIVERSE meta — authenticated commands, authoritative policy checks, transactions and client-safe views. Excludes client request/UI code.
---

- Link `AIVERSE::meta_server`, include `engine/meta/Service.h`. Example: `examples/meta/server`.
  Keep server policy modules, configuration, credentials and DB adapters in the server build.
- Authentication constructs Principal. Host clock supplies time. Never accept authority,
  player identity, prices, grants, validated receipts or match results from player arguments.
- Execute ServiceRequest through `Service::execute`; return result/data or the player view.
  Never expose `save()` or receipt journals as client projections.
- Implement Commit as one atomic compare-and-swap of the aggregate revision, including every
  affected profile and receipt. For the reference SQLite host read `skills/meta-database/SKILL.md`.
- Configure only selected policies and resolve their declared dependencies. Read the recipe
  for the policy you change; client callers only need `skills/meta-client/SKILL.md`.
- Run server policy tests, real authenticated request checks, storage restart/retry checks
  and the server-only example. A local reference host is not a deployed production service.
