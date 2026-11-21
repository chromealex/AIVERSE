# Stage 68 — full migration inventory (2026-11-20)

Branch feature/studio-html5. Documentation implementation 8bade8b27b1d2c569a6e695ff2e8631ffce653a8.

Added docs/studio-html5-parity.md from 49 actual legacy window registrations, plus lifecycle, scene, Game runtime, data/tools, world, rendering/animation and workspace workflow gates. Eight window IDs have partial HTML5 implementations; placeholders are not treated as completion. No runtime/API/save-format changes.

Executed source registration inventory (49 rows). Native/frontend tests NOT RUN for this documentation-only stage; previous Stage 67 results unchanged. CI N/A. Full migration and native visual QA still pending. User explicitly requires full transfer before main merge; main unchanged.

Next: implement typed native entity create/delete/duplicate and Studio UI Hierarchy, native save/undo/reopen tests, then update matrix and commit separate handoff. Remaining major requirements include actual Game runtime and complete legacy windows, not generic placeholder command forms.
