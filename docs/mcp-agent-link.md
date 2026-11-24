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

## HTML5 Studio owner history (S6d)

The authenticated link recognizes four explicit Studio-only asset adapters: `studio_asset_history`, `studio_asset_catalog`, `studio_asset_meta_patch`, and `studio_asset_folder_create`. They are discoverable in the shared MCP registry but refuse local fallback/legacy editor execution. Get the active project and version from history; catalog requires expectedProject; mutations also require expectedVersion. No native picker, arbitrary browser RPC, purge or destructive asset mutation is exposed. Existing Rules/Manifest saves record their native project steps where supported. Agent Panel resolves retained map/project/assets IDs and allows one-step Undo/Redo at that owner's boundary; only an exact agent-only map suffix may use bulk undo. Unsupported operations remain untracked. Mixed scopes are not a global atomic undo transaction.

### S6d.2 capability inventory and internal moves

`studio_history_capabilities` lists the actual linked Workspace registry with an explicit policy per operation: read, map, project, assets, or unverified. This is not a guarantee that an individual call records a step; only actual owner identities count. Unknown operations are never inferred read-only by name, may have untracked effects, and fence bulk map undo. `studio_asset_move_preview` resolves 1–64 existing GUIDs against expectedProject and expectedVersion. Set destination or supply newName for a single GUID rename. Review references and collisions, then call `studio_asset_move_commit` with the same version, the returned token and one explicit import/keep-both/skip decision per item. Agent and browser previews are separate; browser preview is invalidated after a successful agent move. No external paths, replace, delete, purge or arbitrary native dispatch. Exact project/asset version, GUID/path, native dependencies and transfer owner checks remain authoritative.

### S6f.1 coordinated agent batch

Agent Panel exposes reviewed Undo/Redo batches for fully tracked conversations (max 64 steps) involving Map, compatible Project edits and importer metadata. Native preview/apply verifies exact owner versions, ordered step boundaries and explicit confirmation. Existing files and Project checkpoint share a single journal; Map state is staged in memory and remains unsaved until map save. Asset timeline checkpoint follows the commit and uses its existing interrupted-publication fence. Move/create/import/delete/replace plans are refused in this batch version. No durable cross-owner atomicity for saved scene files or recovery payloads is claimed.

### S6f.2 disjoint asset relocation batches

Coordinated batch supports native staged move/rename, quarantine and replacement plans when their paths are disjoint across groups and do not overlap document edits. Native references and byte/digest checks remain authoritative. An outer project-local journal selects rollback or commit of relocations plus the nested existing-file transaction; reopen in Studio for recovery. Overlapping chains and dependencies requiring interleaved publication are refused. Only currently exposed agent operations can produce agent history links. Map remains unsaved and asset cursor checkpoint is separately fenced.

### S6g.1 — asset move dependency preview
Linked `studio_asset_move_preview` includes conservative read-only exact path candidates in the current map and saved JSON maps, alongside existing typed material/rules slots. `document`, `owner` and `field` identify the source (JSON pointer for map candidates); `writable: false` blocks a non-skip decision. Commit independently revalidates references. This is not a complete dynamic dependency analysis or a new mutation API.
