# Script particle handles (#151)

Acceptance covers queued start/move/attach/stop/kill, stale handles and teardown. Runtime checks pending.
8d6783c885f44e5e83a86e95aab8d25f65af6bda
GameViewScript sends handle commands through its presentation request queue. ClientFx owns particle instances and tracks visible generation-safe holders. Runtime acceptance pending.
