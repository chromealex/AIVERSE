---
name: meta-client
description: Connect AIVERSE native host or UI code to meta backends with a persisted request outbox, safe retry and restart recovery.
---

Use `engine/meta/Client.h`; link `aiverse::meta` or the registered native helper.
The Client is host state, outside deterministic simulation. Backend must outlive it.
Backend callbacks and `pump()` run on the same owning thread, never directly on a worker.

- Construct `Client(backend, persist, savedOutbox)`. Persist atomically per authenticated
  account. Reuse operation IDs across process restarts; never replay one account's outbox
  after switching users. Without Persist, recovery is only in memory.
- Use `requests::buy`, `claim`, `chatSend`, `view` or an existing `ServiceRequest` for other
  catalog actions. `boardSubmit` requires trusted authority on the server, not client scores.
  Operation IDs must be unique per player. The server owns prices, balances and permissions.
- `begin` returns false for invalid input, an unresolved request, or failed outbox storage;
  no request is sent in those cases. A true return means submitted, not successfully applied.
- `pump` dispatches replies. While `pending`, do not submit another mutation. Retry after
  a transport failure or restart using `retry()`; it retains the exact operation and arguments.
  Backend maps ambiguous transport failures to `transportFailure(detail)` and owns its timeout/reconnect.
  Server denial/validation errors are terminal. Duplicate means the original result persisted.
- Failed outbox acknowledgement keeps the request pending even after server success. Retry
  once local storage recovers. Do not erase the outbox or generate a replacement operation.
- Save/persist data contains request arguments, potentially chat text. Keep it private and
  per-account. Credentials are supplied by transport and must not be put in arguments.

See `examples/meta/native/Main.cpp`; test `meta_client` covers lost replies, restored
requests, storage failure, duplicate callbacks and destruction before completion.
