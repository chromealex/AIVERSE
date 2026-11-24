# W1.23s

UnifiedCheckpointPublisher captures all four sections, binds SHA256, UUID, sequence and revision to existing generation manifest and reuses atomic publisher. RegionCheckpointPublisher optional unified flag writes UWT1 + manifest prefix; legacy default unchanged. Caller holds lifetime lock and stops old writer. No unified loader/rotation wiring yet; do not mix legacy and unified generations in same directory. Native tests not run. Next unified verified loader and session rotation. PR #116 Draft.
