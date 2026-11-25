# S6e.2 — persisted Assets undo/redo (2026-11-24)

Branch feature/studio-html5; base bd7d1d2060e4009a833be5759bb440c3a61b2c93. Serialization cdedc6fe4be7a97ae759f8bda1a6ea2c8e197c4d; native integration e3725a7fcf7dc137d854b7f8530a974f7c1f01b2.

- Persistent checkpoint stores typed metadata edits, internal moves, creation/import, deletion and replacement history with cursor and bounded retained payload. Existing asset payloads stay in original locations or recovery slots. Fresh epoch on reopen.
- Checksummed checkpoint verifies exact entire content tree, materials and referenced recovery-slot tree hashes. Rejects unsafe paths, malformed plans, missing slots, changed bytes, oversized history. Existing replay owners still validate dependencies and identity at execution.
- Native and linked agent mutations share a publication wrapper, including undo/redo and explicit Trash operations. Advisory lock and dirty marker fence interrupted checkpoint publication. On failure, exact unchanged prior checkpoint can be retained; otherwise warning and cross-session replay refused. Asset journal recovery acquires same cooperating history lock before open. Native open/CLI load saved timeline; legacy MCP project-switch route remains conservative and requires native reopen.
- Added contract sources for mixed asset timeline across reopen, redo, stale epoch, interrupted marker, external content and backup corruption. History UI text updated.

Limits: no atomic checkpoint+asset journal commit; a crash between asset commit and checkpoint may discard the timeline but retains recovery slots. Full-tree hashing is synchronous, can be slow on large projects, and refuses trees over 20k entries or unsupported entries. No lock against legacy/external writers; no full cross-editor session baseline coordination. Map history persistence and cross-owner coordination remain outstanding. No compilation or tests executed in this stage; no main merge.

Next S6e.3 persistent Map timeline, then cross-scope coordinator, dependency coverage, storage policy, MCP coverage and integrated QA.
