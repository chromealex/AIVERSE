# S6f.4b.1 — creation-aware Studio save (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 9523982b4342107e20b76c35c9136e8c9ef6de5e. Journal 6c5b9fa55f9b98951ae8632308afe785c7dcef97; JSON type correction d4c55d73cca7e6ea754fead477f8a2a1578165d3; map staging 0e462531892686b7172e4be98f2819a9f7afaa33; Studio dispatch c1997cfffd7ab77d1e7fce8971b6a2841f383cb5; fixtures d3b791d2c4d698e3d27a3e6264a7e54dc3f027b5.

- ProjectDocumentJournal v2 supports explicit create-only snapshots, including empty outputs, no-clobber hard-link publication on POSIX and non-replacing MoveFileEx on Windows, and rollback of matching created bytes. v1 manifests remain readable. Creation is restricted to map JSON, R16 and splat files under existing safe maps directories; it is not a history edit.
- Studio Save and Save As share detached map/project staging. New map and terrain outputs, dirty Rules/Manifest, and Project history checkpoint use one journal. New map registration is staged in the manifest. Save As refuses occupied new map identities and sidecars; no implicit parent creation.
- Added fixture for every publication prefix, empty created file, recovery conflict and occupied Save As. Source and fixtures committed, NOT BUILT OR RUN. No main merge.
- Remaining: map history checkpoint/dirty marker still separate, asset history checkpoint separate; standalone Workspace map_save unchanged. Map disk baseline is sampled at staging, not retained at load. Identical-byte external creation cannot be distinguished during recovery; advisory lock applies to cooperating writers. Windows and hard-link filesystem compatibility require validation.

Next S6f.4b.2: staged map checkpoint and retained exact map/sidecar baselines, then asset cursor durability, S6h.3 recovery and S6i.3 MCP. 
