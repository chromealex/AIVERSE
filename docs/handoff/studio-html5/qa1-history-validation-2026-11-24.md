# Studio HTML5 QA.1 — first static fixes and focused C++ checks

2026-11-24, branch `feature/studio-html5`. Initial HEAD `0e86525f75dbeac75aa817272f9c1e2359ec1965`; fixes `609f3acfe7d221a0fd996316358aebf4811a715c`.

## Actual findings and fixes

- Static inspection identified a missing struct closing brace in StudioMoveJournal.h. Fixed; this was a native compile blocker.
- Asset journal sync previously unconditionally threw on Windows. Shared editor file publication primitives now provide Win32 FlushFileBuffers / MoveFileExW WRITE_THROUGH and existing POSIX sync. Windows native workflow compiles and runs transfer and move-journal contracts as well as session contracts. Windows power-loss directory durability remains unverified.
- Studio CI now triggers on editor core, shared SHA-256 and root CMake changes too.

## Executed local evidence

Git clone in the Linux tool environment fails DNS resolution for github.com. Three source headers were reconstructed from GitHub connector content and verified with git hash-object against the exact returned blob SHAs:

- ProjectHistory.h: 5cfb69b6a99bd7b701354d4a5209ef09282560bc
- ProjectFileEdits.h: 9a1945808c58dd89cd27eca9ce270d38eb4b4bd0
- StudioAgentHistoryPolicy.h: 38ea52cfd09474770a447da905068981a7414585

A focused standalone harness compiled with GCC 14.2, C++20, -Wall -Wextra -Werror -pedantic and exited 0: distinct owner epochs, stable step IDs across undo/redo, no-op and redo branch fencing, bounded eviction, typed move recording, oversized history barrier, explicit agent policy (7 PASS). Equivalent contract added to the headless script. No full repository compilation, filesystem journal execution, TypeScript/Vite or native UI acceptance claimed.

## CI

Initial Studio check run 45360664987 was cancelled after the fixes push. Run 45361221685 for 609f3acf was pending at inspection. New handoff/contract commit triggers a fresh Studio CI run. Inspect its exact HEAD and all jobs before merging.

## Next

Wait for actual CI results (do not treat pending as PASS). Fix compiler/test failures from logs. On macOS run tools/studio/verify_mac.sh using KINGSTON, including native UI/Metal and history workflows. No main merge.
