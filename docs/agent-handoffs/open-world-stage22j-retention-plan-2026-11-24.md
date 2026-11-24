# W1.22j — retention planning

RegionCheckpointRetention.h adds dry-run plan of complete older checkpoint/WAL pairs, minimum previous generation count and soft byte budget. Active, future, orphan and symlink files protected. Budget counts recognized regular generation files, not total directory usage. Planner does not verify backup contents or delete files; execute only under session lock after active manifest validation. No native tests run. Next validated cleanup execution and session API, then skill/catalog. PR #116 Draft; no main merge.
