# Stage 66 — graph document lifetime

Parent 8b57766a, PR #183. Reuse existing graphDocumentEpoch, propagate through graph panels, key roots by epoch, reject stale supplied epochs in Studio generic operations, shader save and asset metadata patch. Existing callers without epoch remain compatible. Native regression checks invalid and previous document epoch. This guards dispatch, not cancellation of already running GPU jobs. Build/typecheck/tests NOT RUN. Main untouched.
