# Notice collection (#178)

293b9f8b80d30c7b12c85d7c060fd4437b319359 extracts the actual client collector in apps/client/NoticeEvents.h and registers a Simulation::advance / loadState / present regression in tests/tests.cpp. The first fixture (empty match) failed at batch.size() == 3. The committed fixture has a live unit for each player. Rerun deferred until the full backlog has been implemented.
