# W1.20f — legacy and race seat notifications

Commits `a99106db`, `6f87d6b4`, PR #116 Draft. ActorSystems.cpp explicitly journals arcade enter/exit, damage driver release, knockdown/death and destroyed vehicle driver release. ActorVehicles.cpp journals RaceSeats seat and respawn for car/passengers. Existing persistent crossing notifications unchanged. This covers inspected writer sites, not a proof of all writers. Index still uses canonical scan; no native build/tests run. Next integrate event-driven index with verified writer coverage, update skill/catalog. No main merge.
