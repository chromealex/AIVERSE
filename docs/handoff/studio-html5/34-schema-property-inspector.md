# AIVERSE Studio HTML5 — Stage 34: schema-validated Inspector inputs

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 254eb60692bcc824d37148c518164ad760282e06
Stage commit: see branch history.

## Completed
- Added pure, framework-independent propertyLogic.ts validation used by AI-authored Inspector panels.
- Numeric drafts must be nonempty, finite, within optional bounds and optionally safe integers; no silent rounding or clamping.
- Options/enums are validated against the actual value type; read-only and boolean fields cannot be written through the wrong control.
- Replaced previous HTML5 PropertyField with validated controlled input, inline errors, native/select/checkbox support and Escape-to-revert behavior. Prevents the previous bug where an empty numeric input wrote 0 on blur.
- Added numeric field metadata (min/max/step/integer) from StudioSession C++ while keeping Workspace authoritative for every project mutation.
- Added Node 22 field tests to frontend CI and npm scripts.

## Files
apps/studio-web/src/propertyLogic.ts
apps/studio-web/src/components/PropertyField.tsx
apps/studio-web/src/types.ts
apps/studio-web/tests/propertyLogic.test.mjs
apps/studio-web/package.json
apps/studio-native/StudioSession.cpp
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/34-schema-property-inspector.md

## Tests actually executed
- Node.js 22.16.0 test of the standalone propertyLogic.ts with 8 cases: **PASS 8/8**, zero failures.
- tsc 5.8.3 --strict --noEmit on standalone propertyLogic.ts: PASS.
- The full React/Vite build and macOS WKWebView/C++ binary: NOT RUN (no GitHub checkout/network to install frontend modules on the Linux runner).
- GitHub Actions was queued when checked; no PASS claim.

## Risks / next
- Existing native Workspace validates edits independently; TS validation is user-experience protection, not an authorization boundary.
- React form rendering and actual browser behaviors require Safari/WKWebView tests.
- Add SchemaForm groups and reusable property descriptions for more editor subsystems.
- Fix any CI failures once actual macOS/web runners execute; retain ImGui production editor until parity.
