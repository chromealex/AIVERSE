# Stage 90 — VIVID preview phase and motion evidence

Native Studio preview now calls shared playHumanPreview: 0.65s crossfade and
preserved facial/breath phase. Animator Play.startTime defaults to zero and leaves
old callers unchanged. No bridge/schema/web UI changes. Idle generator adds spine
breathing, subtle resting arms, gaze and asymmetric smooth blink envelopes.

See docs/qa/vivid-motion-review-2026-11-20.md for actual checks, initial test failure,
final2/2 pass, videos and remaining hair/eyes/T-pose shirt defects. UI parity NOT RUN.
All builds/captures on KINGSTON. Exact code SHA is recorded in the separately
committed VIVID and integration handoffs. Existing source texture repaired with
backup; older assembled packages remain unchanged. No main merge or push.
