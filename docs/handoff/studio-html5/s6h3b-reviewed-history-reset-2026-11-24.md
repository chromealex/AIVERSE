# S6h.3b — reviewed damaged Asset history reset

Branch feature/studio-html5; base e8e66f8332bffa7fd9e2c57a773315ab0fe694c0. Native recovery 7a2402986df8384c2630022c76c8d2ba4af2158e, session b74792ea10907693f6d40ca7e287d05e2a70ee52, UI 8927761d363beef74be7cdb36b2ae03a0b22a441, fixture 08e0cea8668b58a52a6da13c817b32034eb7e6a7.

- Explicit review and confirmation archive exact damaged checkpoint and legacy marker bytes, publish an empty checkpoint through ProjectDocumentJournal, and retire the legacy marker only after commit. Review refuses healthy history and active journals. An interruption after checkpoint commit but before marker retirement can require another reviewed reset; extra archives are retained.
- Trash dialog exposes review, confirmation and native diagnostics. Native bridge binds expected project and history version; demo mode refuses. No live assets or Trash payloads are modified by reset.
- Fixture covers stale review, damaged checkpoint, legacy marker, archive preservation, healthy checkpoint refusal and pending journal refusal. Source and fixture committed; build, CTest, native/UI tests and actual fault injection NOT RUN.
- Limits: reset does not reconstruct damaged history, archive cleanup is manual, unsafe/oversized files require manual inspection. No main merge. Next S6i.3: complete MCP discovery and owner routing, then comprehensive QA.
