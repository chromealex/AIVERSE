# fx-model-shadow-option — initial handoff

Owner: spectrafx
Branch/SHA: pending
Architecture/API: pending
Changed paths: pending
Tests: NOT RUN
Risks: pending
Next: confirm owner, update roadmap and implement.

## 2026-11-27 — implementation checkpoint

Commit: `8be0f0c0d17500ced8dfaf76b5e91fa7471340c6`
Branch: `codex/issue223-minimap-atlas-20261126`
Paths: `engine/fx/AnimatedModelEffectView.h`, `engine/fx/FxEffect.cpp`, `engine/fx/FxEffect.h`, `tests/tests.cpp`

Source changes committed; readiness is pending. Tests/build: NOT RUN for this revision, per request to build the complete batch once at the end. `git diff --check` passed before commit. No save-format migration introduced. Existing atlas-only ImageSource::load semantics preserved; new loadImage handles both storage forms. Remaining work: review and verify controls, Lua selection, voice sequencing, FX and projectile paths; add missing runtime/Studio/publication evidence. Issues #211–213, #219, #221 remain unimplemented; #206 requires Windows crash verification. Do not merge or close issues from this checkpoint. Next: finish remaining issue implementations, refresh main, run the final merged-tree Mac gate and feature readiness checks.

## Material effects example plan — 2026-11-27

Prepare one ordinary data-only demo for shadows, both additive model modes and linear/display emitters, reusing the verified shadow fixture. Existing owner APIs only. Native/Web acceptance and publication pending.
