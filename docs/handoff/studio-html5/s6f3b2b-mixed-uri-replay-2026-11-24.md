# S6f.3.4b.2b — mixed typed URI replay (2026-11-24)

Owner Studio; branch feature/studio-html5. Base d0ee4c429ef5e791583d5196c367dc2b13b92cfe. Implementation fcc5970de6892c79c699ed68838d38b6f917c7f6; fixtures 4df5a7cee64a3510431a315917890ec6692e756a; recovery origin fence 01dcbdc2f3b3a498b0b7d9d15d28e0da4ac35ca2.

- Mixed planner accepts recorded glTF/GLB URI repairs, resolves pre-step owners to physical origins, composes exact before/after bytes, validates directional digests and rejects non-URI JSON edits. Both stationary and moving repair owners enter the projected graph. Repaired physical models preserve every external slot's target GUID. Streamed GLB prefix snapshots retain unchanged tail digest/length. Journal v3 is reused.
- Added mixed folder relocation, texture relocation and metadata fixture with stationary glTF and moving streamed GLB owners; checks exact bytes, GUIDs and forged non-URI edit refusal.
- Model repairs with physical origins or destinations in Trash, unknown extensions, other repair formats, nested relocation roots and interleaved map/rules dependencies remain limited.

Evidence: source and fixtures committed, NOT BUILT OR RUN. No native MCP, UI, CTest or fault-injection execution evidence; no main merge. Next S6f.4 cross-owner durable save, then S6h.3 recovery, S6i.3 MCP integration and final QA. Review mixed URI edge cases and verify full fixture suite before declaring production-ready.
