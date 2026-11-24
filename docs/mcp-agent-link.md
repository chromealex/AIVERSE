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

### S6g.2 — glTF dependency edges
Existing linked move preview includes read-only external glTF buffer/image resource edges; either endpoint may block the move. Native Studio graph query accepts an optional indexed asset path; no new standalone MCP tool in this stage. Embedded/remote/unsupported URIs, GLB, scripts and binary dependencies are outside coverage.

### S6g.3 — bounded dependency traversal and relocation validation
Native studio.assetReferences optional path/depth returns direct or multi-hop edges (incoming/outgoing, depth and cycle-closing mark); no standalone linked query yet. Existing linked move preview proves only a single conflict-free relocation with unchanged supported glTF URI resolution. Native transfer owner checks actual destinations and replay; no automatic rewrite. Coordinated history and deletion retain stricter fences.

### S6h.1 — recovery storage policy
Native Studio Asset Trash reports logical trash/checkpoint/journal bytes, free disk and configurable retained quota; positive net payload growth is refused above quota. Default unlimited; manual purge remains explicit. Standalone linked quota API deferred to S6i; no automatic cleanup.

### S6h.2 — reviewed history-protected batch cleanup
Studio Trash supports explicit selected-record batch review/apply with exact digest and history version protection. Native read/write operations are not yet standalone linked MCP tools (S6i). No automatic eviction; single-record purge retains its documented history-reset semantics.

### S6i.1 — standalone linked asset and recovery tools
All require authenticated Studio and expectedProject from `studio_asset_history`. `studio_asset_dependencies` takes guid, optional depth (1–16), offset/limit (max 100); returns items,total,guid,path,version. `studio_asset_recovery_storage` and `studio_asset_trash_list` read actual storage and records. `studio_asset_delete` requires guid, current expectedVersion and confirmed=true; moves to recoverable Trash and records asset history. `studio_asset_trash_restore_review` takes id; `studio_asset_trash_restore` requires id, returned token/version and confirmed=true; restore resets asset history. `studio_asset_recovery_quota_set` uses expectedMiB/quotaMiB CAS. `studio_asset_trash_cleanup_review` accepts 1–50 IDs; permanent purge remains native UI only. Read tools are explicit read policy, delete assets policy, restore/quota unverified barriers. Local fallback refuses all. No external import picker or arbitrary filesystem access.

### S6i.2 — linked conversation history navigation
`mcp_registry` now reports `studioLinkOnly` independently of `guiOnly`; linked capability inventory carries the same flag. External authenticated clients use `studio_agent_history_list` with expectedProject and optional offset/limit, then `studio_agent_history_step` with scope, historyId, direction, expectedVersion and confirmed=true. Batch uses `studio_agent_batch_preview` and `studio_agent_batch_apply` with exact direction/token and confirmation. Hosted agent must be stopped; existing owner boundaries remain authoritative. History navigation is not a new edit. Standalone audit explicitly checks fallback refusal. Permanent purge and external picker remain native-only.

### S6g.4 — GLB and Wavefront dependencies
Dependency queries include GLB external JSON resources and OBJ material libraries/MTL texture slots actually read by the engine. Multi-item move preview distinguishes final native resource validation from unconditional read-only document blockers; commit validates actual destinations including selected conflict decisions. No URI rewrite.

### S6g.5 — paged graph and diagnostics
Linked dependency query now returns native bounded pages, token and coverage summary; optional expectedToken fences changed selected graphs and direction requires depth. COLLADA declared image paths are conservative candidates. Unknown formats and dynamic references remain explicit coverage gaps.

### S6g.6.1 — stationary glTF path repair
Existing linked internal move preview advertises repair candidates; commit may repair supported stationary textual glTF buffers/images URI slots transactionally. Exact undo/redo bytes retained. Other resource formats and moving-source rewrites remain conservative. No additional MCP command.

### S6g.6.2 — moved glTF owners
Internal move uses projected owner paths for textual glTF URI repair, including nested folder moves. Direction-specific file/tree digests and v5 recovery maintain exact original bytes. Legacy v4 recovery retained. No arbitrary source rewriting or coordinated repaired batch replay.

### S6g.6.3 — binary GLB URI repair
Internal moves repair bounded GLB v2 JSON chunks with exact trailing chunk preservation, binary-safe history and v6 journal recovery. Models exceeding retained repair budgets are refused before mutation. Existing MCP operation and preview fields are unchanged.

### S6g.6.4 — Wavefront and COLLADA repair
Native move owner now plans supported OBJ mtllib, MTL texture and DAE leaf image paths with exact text-span edits and shared MTL conflict refusal. Existing linked preview/commit use the same owner; Round-trippable UTF-16 LE/BE is supported; mixed XML leaf rewrites and unrepresentable Wavefront texture names are refused.

### S6g.6.5 — streaming large GLB repair
Binary GLB above 1 MiB uses compact JSON-prefix history and verified streamed tail publication through journal v8. Existing linked internal move automatically uses the same owner. Temporary disk space, JSON-prefix budget and GLB format limits remain explicit; no large text/other model streaming or coordinated repaired batch yet.

### S6f.3.1 — ordinary move chains
Linked agent batch apply now plans ordinary exact-endpoint move chains and stationary resource repairs together; compact GLB snapshots use coordinated journal v2. Mixed dependent Trash/replacement, moved-owner repairs, nested endpoints and ambiguous cycles remain fenced. Existing preview token still fences owner history versions, with filesystem validation on apply.

### S6f.3.2 — moving-owner repair chains
Existing linked agent batch apply supports ordinary exact-endpoint chains with changed moved model/file/tree digests. Virtual repair overlays validate the original physical identity and intermediate recorded bytes; journal v3 publishes final edits and recovers partial publication using reverse overlays. Nested endpoints and mixed dependent operations remain fenced.

### S6f.3.3 — mixed file history chains
Linked agent batch uses mixed file planner for supported texture/audio/.bin import, move, settings, replacement and quarantine chains. Sidecar identity follows content/Trash endpoints; v3 journal recovers exact metadata. Unsupported mixed model or nested-directory chains remain conservative.

### S6f.3.4a — mixed directory and embedded model chains
Existing linked agent batch may coordinate exact-endpoint directory moves/quarantine and self-contained glTF/GLB version transitions with nested GUID checks. External URI/extension topology, nested relocation endpoints and nested Trash metadata changes remain fenced.

### S6f.3.4b.1 — stable external model versions
Existing linked batch planner resolves local glTF/GLB buffer and image URIs in outgoing and incoming replacement versions at the logical content owner, including recovery slots. Targets must be safe indexed regular files whose identity is unaffected by any relocation endpoint. External model moves, changed dependency endpoints, extension topology and mixed URI repair remain fenced. No new MCP action or journal version.

### S6f.3.4b.2a — projected local model dependencies
The mixed exact-endpoint planner now resolves typed glTF/GLB external resources against virtual content occupancy after each complete step, including paired file moves, directory moves and model version exchange. A surviving physical model must keep its resource GUID per slot. Unmanaged owners and other formats retain conservative reference fences. Mixed URI repairs, nested relocation roots and unknown model extensions remain fenced. No new MCP action.

### S6f.3.4b.2b — mixed typed URI replay
Existing mixed batch planner now accepts recorded glTF/GLB URI repairs, including stationary and moving owners, compact GLB prefix snapshots and importer settings in the same history chain. Directional digests, byte continuity, unchanged non-URI JSON and per-slot target GUIDs are checked before journal publication. Other repair formats, unknown extensions, nested relocation roots and model edits originating/ending in Trash remain fenced. No new MCP action or journal version.

### S6f.4a — coordinated save of existing owner documents
Studio `studio.save` with dirty Rules or Manifest stages the existing map and emitted terrain sidecars together with dirty project documents and the project history checkpoint, then commits one existing-file journal before replacing live owners. New sidecars, Save As, asset cursor checkpoints and atomic map checkpoint publication remain separate. This is not a new MCP action or a guarantee for standalone map_save.

### S6f.4b.1 — creation-aware Studio save
Native Save and Save As use one creation-aware document journal transaction for map JSON, emitted terrain files, dirty Rules/Manifest and Project history checkpoint. Journal v2 distinguishes absent outputs from empty files and retains v1 recovery compatibility. New map destinations and sidecars must be vacant, with existing safe parent directories under maps/. Save As adds its map to the manifest in the same transaction. Map/asset cursor checkpoint atomicity and standalone map_save integration remain outstanding. No new MCP action.
