# W1.20f — bidirectional seat index

Commit `6d2b2a61c34945e8462c228b9b0070ba76c01c54`, PR #116 Draft. ActorRegionOwnership.cpp adds membersByCar alongside carByMember and updates both via eraseMember/setMember for detected membership differences. Unchanged ticks retain previous resource. Canonical full scan and copy of index on changed ticks remain; this is delta application, not direct seat-event wiring or scan-free mode. Restore hook erases resource. No native tests run. Next: wire actual seat writers, avoid full copy, verify configuration and update skills/catalog. No main merge.
