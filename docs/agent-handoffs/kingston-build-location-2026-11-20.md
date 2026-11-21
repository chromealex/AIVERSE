# KINGSTON build output policy

Date: 2026-11-20
Branch: feature/studio-html5
Instruction commit: 0a01fb13dcf8039734b8d63cae9e71f915e85d9e

Owner explicitly requires all builds on KINGSTON. AGENTS.md now overrides
local/worktree/release build examples: use isolated directories under
/Volumes/KINGSTON/AIVERSE-builds for build output, intermediates, temporary
files, reports and packages. Do not fall back to internal storage when absent.
Source location is unchanged; existing/running builds were not moved/deleted.
Created the external root directory successfully.

Validation: df confirmed KINGSTON mounted with about 746 GiB free; mkdir
succeeded; git diff --check passed. No build/tests run (documentation only).
No public APIs, save formats or runtime behavior changed. CI not run.

Next: every future configure/build must explicitly select a unique external
build directory. Configure frontend output/cache paths too. This rule is
committed locally on the active Studio branch; propagate to other active
branches/main through normal integration. Existing local build relocation
is separate work and must preserve active processes and CMake source paths.
