# W1.39 Actor ownership GCC build repair

Code commit 63fc5e0cd on codex/open-world-acceptance-20261124. GitHub Actions job 138771785151 failed on Linux arm64 with -Werror=missing-field-initializers in engine/actor/ActorRegionOwnership.cpp. Explicitly value-initialize the rollback entity vector. The rollback and ledger transaction semantics are unchanged.

MinGW GCC 16 x64 syntax check of the full translation unit passed with -std=c++20 -I. -Wall -Wextra -Wpedantic -Wno-misleading-indentation -Werror -fsyntax-only. Linux CI rerun pending at handoff. Next gate is the complete merged-tree Mac quick suite on KINGSTON. The wider OW-01 scope and its outstanding runtime acceptance are listed in the stage 38 handoff.
