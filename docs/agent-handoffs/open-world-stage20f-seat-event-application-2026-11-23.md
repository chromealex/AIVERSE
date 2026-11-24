# W1.20f — candidate seat index application

Commit `55ba94ac3a7b65e38ed22fd80c1e82ba8cc09b8f`, PR #116 Draft. ActorRegionOwnership.cpp adds eraseCar and replaces candidate groups in a staged bidirectional index. Compares result to canonical membership and rebuilds on mismatch, bootstrap or overflow as needed. Full canonical scan and index copy still remain; this is event-driven application with validation, not scan-free discovery. No native tests run. Next remove full scan only after complete writer coverage, add diagnostics and update skills/catalog. No main merge.
