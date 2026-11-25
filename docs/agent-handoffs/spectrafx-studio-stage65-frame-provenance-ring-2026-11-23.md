# Stage 65 — frame provenance ring

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits a633ed2f5eb9e9275159956212fc494d20eb4fb6, 0c198b63842a75b7934325b521d8e80b758fc8d8, aea5088090b44f5f5e1604514805fb675b35ec11.

StudioScene.h/cpp add 64-entry rendered-frame status ring and tagged callback matching. StudioHost.cpp replaces request-time tick/camera/generation with matched frame metadata and rejects unmatched replay-ready captures. Skill updated. No tests/builds. Audit callback lifetime, serial uniqueness, capture generation/project changes, replay readiness and non-Metal fallback before QA. Next add request identity checks and capture failure handling.
