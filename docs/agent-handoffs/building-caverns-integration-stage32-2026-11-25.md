# Linux ARM64 GCC aggregate replacement

Source 5d6e08f3937c46bc198f93564160c4e645aaf631 changes engine/core/ECS.h and tests/ecs_cow_exception_test.cpp. The [original Actions job](https://github.com/chromealex/AIVERSE/actions/runs/45560284050/job/139022086091) hit GCC 13.3.0 gimple_add_tmp_var in both Economy.cpp and GameCore.cpp when instantiating Pool<Combat>::emplace with an empty argument pack. The Linux AArch64 cross-compiler reproduced the same ICE in each original translation unit.

The paged replacement path now names the brace-initialized component before moving it into the existing slot. The new regression covers contiguous and paged storage, default initialization of nested array members on replacement, cardinality and snapshot isolation. Runtime API, format and component layout are unchanged.

The [recorded verification](../evidence/ecs-pool-release-5d6e08f39.json) on the committed source compiled both complete translation units with GCC 13.3.0 for Linux AArch64 using the CI Release warning and optimization flags. Both ELF objects were identified as AArch64. Six Mac CTests passed, including multiworld, snapshot delta, COW snapshots, exceptions, parallel writes and rollback sessions. The [raw log](../evidence/ecs-pool-release-5d6e08f39.log.txt) contains compiler commands and timings.

The hosted Linux job for the source commit was queued at verification. Local cross-compilation covers the two formerly failing units. Full hosted Linux execution, Windows runtime, merged-tree Mac quick and the remaining strict feature acceptance criteria are still required for main.
