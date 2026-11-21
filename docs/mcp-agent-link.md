# Editor agent link (aiversemcp <-> editor), protocol 2

The running editor listens on `127.0.0.1:<port>` (default `47321`, see docs/overview.md
"Several editors at once"); `aiversemcp --port <port>` forwards MCP tool calls to it as
newline-delimited JSON. Since 2026-11-21 the link is authenticated, because any local
process, and any web page through a browser's no-cors `POST http://127.0.0.1:47321/`,
could otherwise reach it and run editor tools.

## Session token

- When the editor starts listening it generates a random 256-bit token (64 hex characters)
  and writes it to `<home>/.aiverse/agent-link/<port>.token` (`HOME`, on Windows
  `USERPROFILE`; the folder can be overridden with `AIVERSE_AGENT_LINK_DIR`). On POSIX the
  folder is `0700` and the file `0600`; on Windows the user profile ACL keeps it private.
- The file is removed when the editor stops listening (only if it still holds this editor's
  token). A restarted editor writes a new token.
- The port remains the discovery mechanism (as before); the token file is keyed by it.

## Handshake

The first line a client sends must be

```json
{"id": 1, "method": "hello", "token": "<contents of the token file>"}
```

Reply: `{"id": 1, "result": {"ok": true, "protocol": 2}}`. After that the client sends
`{"id": n, "method": "call", "params": {"name": ..., "arguments": {...}}}` as before.

The editor closes the connection when:
- a line is not JSON (or not a JSON object);
- a line looks like an HTTP request line (`GET `, `POST `, `PUT `, `OPTIONS `, `HEAD `, ...);
- a line is longer than 4 MiB, or 4 MiB arrive without a newline;
- the token is wrong, or a `call` arrives before a successful `hello` (an error reply is sent
  first).

## Compatibility

There is no older discovery file to stay compatible with: an aiversemcp built before this
change (no `hello`) is refused with an error telling it to update. aiversemcp re-reads the
token on every (re)connect and, if the editor refuses, prints the reason to stderr and falls
back to its local workspace as it already did when no editor was running.

Code: `apps/editor/core/Net.{h,cpp}` (`LineServer`, `agentTokenPath`, `readAgentToken`),
`apps/editor/EditorAgent.cpp` (`Editor::pollAgentLink`), `tools/mcp/main.cpp`
(`EditorLink::connectAndHello`). Test: `tests/editor/agent_link_tests.cpp`.
