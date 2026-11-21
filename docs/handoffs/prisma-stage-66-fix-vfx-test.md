# PRISMA Stage 66 — локальная Mac-сборка: устранение VFX test compile blocker

Дата: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Related handoff: docs/handoffs/prisma-stage-66.md
Source patch: `049188f737e1d17a638d4762c5dce6e5cadc3db5`
Do not merge main/base without explicit user approval.

## Actual local evidence (supplied by user)

On macOS in
`~/Projects/AIVERSE-prisma-validation-20261119-222509`,
during the consolidated Stage-66
`cmake --build build/prisma-final --parallel 4`,
the whole build stopped at 66%, with clang diagnostics:

```text
tests/vfx_ecs_presentation_test.cpp:87:17: error: use of undeclared identifier 'particles'
    ps.snapshot(particles);
tests/vfx_ecs_presentation_test.cpp:88:11: error: use of undeclared identifier 'particles'
    check(particles.empty(), "reset must kill attached and recent one-shot particles");
2 errors generated.
make[2]: *** [CMakeFiles/aiverse_vfx_ecs_tests.dir/tests/vfx_ecs_presentation_test.cpp.o] Error 1
make: *** [all] Error 2
Exit code: 2
```

Log ZIP on the user's Mac:
`~/Projects/AIVERSE-prisma-validation-20261119-222509/prisma-validation-logs.zip`.
The actual ZIP bytes were not uploaded to this chat; the above
failure evidence comes exclusively from the pasted terminal output.

This failure is in a **SpectraFX GPU-free test fixture**, not in
PRISMA runtime, shader code or renderer. The whole build terminated,
so the full CTest, Metal/OpenGL HDR and GPU visual tests **DID NOT RUN**.

## Investigation and patch

Compared the exact file content on
`feature/prisma-postfx`, `main` and
`feature/spectrafx-generic-cue-queue` through the connected GitHub
repository. The first had 97 lines and referenced an undeclared
`particles` at lines 87/88. Both upstream `main` and SpectraFX
branch already had the same corrected 109-line version with:

- `#include <algorithm>`
- `std::vector<fx::FxParticleInfo> particles;`
- `ps.effect("impact_flash")` for effect ID
- `ps.snapshot(particles)` and `std::count_if` to count only the
  three one-shot burst particles, including after a duplicate tick
- Reset test still requires the snapshot vector to become empty.

This is stronger than adding an isolated local declaration:
the old unsigned `before-20` assertion could underflow and
was not a reliable count of the event's own particles.

The connected GitHub version of `engine/fx/ParticleSystem.h`
supports `FxParticleInfo`, `effect(name)` and `snapshot(...)`.
Therefore copied the exact upstream corrected file to
`feature/prisma-postfx` without modifying VFX implementation
or any PRISMA source.

**Patch commit**:
`049188f737e1d17a638d4762c5dce6e5cadc3db5`
file: `tests/vfx_ecs_presentation_test.cpp`
Resulting content blob: `569604dde59cfd624c19b2aa4c09a20312c7fe1d`.
This blob is identical to `main` and the SpectraFX branch
when inspected. Exact source-match is NOT proof the file compiles.

## Actual checks and limitations

- PASS: GitHub readback of affected code on three branches,
  upstream file identity, and API declaration lookup.
- NOT RUN: local clang incremental rebuild after this patch,
  CTest, GPU HDR test executables, screenshots, shader runtime,
  GPU memory/fence stress, Linux/macOS GitHub-hosted workflow.
- The earlier user build is an actual **FAIL** (exit code 2),
  now has a source fix awaiting compilation.
- Do NOT claim Stage 66 or the PR is fully verified.

## Exact next user actions (safe for the detached validation worktree)

```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
bash run-prisma-validation.sh
```

This reuses `build/prisma-final` for incremental native rebuilding;
it does not recreate a different worktree and does not merge main.
`git switch --detach` must be used ONLY when
`git status --short` shows no conflicting tracked modifications.
The existing untracked script and build files remain in place.
If more errors appear, share the new `prisma-validation-logs.zip`
or paste the first compiler/CTest/GPU error.

Another agent must append actual later results to a new handoff,
not overwrite this user's observed initial failure. Preserve
the Draft PR and no merge without approval.
