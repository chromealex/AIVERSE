# W1.20e — actor ECS journal bootstrap

Implementation `49baf73037ab80f9b366cb7dc5d7712b41d51620`, feature/aiv20261123_open_world_audit, PR #116 Draft.

ActorRegionOwnership.cpp enables CharacterBody, Vehicle and Loot tracking lazily whenever region ledger is present. This also re-enables tracking after native restore; the existing full reconciliation bootstraps and covers overflow/raw pool writes. Drain all three transient journals only after successful reconciliation and persistent dirty ACK. No persistent schema or checksum change from journal accesses. No native tests run. This is lifecycle integration, not dirty-only processing: journal entries are not yet used to select transfers. Next audit all writers and introduce safe candidate processing with explicit full-scan fallback, then update skill/catalog. Do not merge.
