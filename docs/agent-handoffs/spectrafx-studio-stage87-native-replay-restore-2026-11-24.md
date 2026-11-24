# Stage 87 — native replay restore and cancellation

Parent `fd7ae46f18824ef7da316587a2f6851e0c9e1f78`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

Added one replayRestore command shared by Studio and MCP: validates camera/stage/playback, optionally loads a graph via Stage 83, prepares the runtime on the render owner, then applies stage/camera. No interleaving RPCs between setup steps. Pending convergence rejects conflicting mutations and native camera navigation; status/Stop/replayCancel remain available. Cancel and 30-second timeout discard unfinished replay and schedule GPU clear on the next owner frame (not rollback to the previous particle state). Optional graph load is individually transactional but is not rolled back if later replay validation fails. UI shows tick progress and cancellation. Native fixture covers pending guard, convergence and cancellation. No tests or builds executed; hardware QA and MCP end-to-end remain outstanding. Main untouched.
