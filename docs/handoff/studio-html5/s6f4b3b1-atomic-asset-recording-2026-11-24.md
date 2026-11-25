# S6f.4b.3b.1 — atomic recording of staged asset operations (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 630610294c784cc80b7e30d054f905a05d4a8605. First checkpoint journal d209cac4da5601e411a82eb9c4f0aebd994b9a1f; staged mutation dispatch 223b20fccad1f040d5fd5ebfc9781ba193a8918b; external transfer dispatch fb54e8423301c8631ed38cf0790aab1ae5152a1a; fixture c178f2a20f74401220f00731cda0255b96524ed2.

- Outer journal v5 supports an absent initial Asset checkpoint, no-clobber publication and rollback of matching checkpoint bytes only after the readiness marker. Existing checkpoint transactions use v4.
- Native metadata edit, folder creation, reversible deletion, external import and replacement now stage file operations and a detached next history before publication. Live history is swapped after the outer commit. Existing catalog identity checks are reused; index refresh remains a disposable post-commit operation.
- Legacy internal move/rename with material repair and explicit Trash restore/purge/batch cleanup still retain the dirty-marker path. Private staging slots may remain on rejection; no automatic purge. No new MCP action.
- Added staged folder/import/metadata/delete recording fixture, including first checkpoint creation. Source and fixtures committed; build, CTest, native QA and actual fault-injection NOT RUN. No main merge.

Next: S6f.4b.3b.2 stage internal moves/renames and material reference edits; then explicit recovery operations, S6h.3, S6i.3 and full QA.
