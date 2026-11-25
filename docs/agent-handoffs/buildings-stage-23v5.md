# Stage 23V.5 — integrated elevators and shared building graph lifecycle
Parent: 7867568e139a1f28c647b2901dfcd33af1175534. Target branch feature/aiv20261123_building-grammar, PR #114.

- Removed explicit elevator rejection from BuildingWorldRuntime. Common scene registry initializes existing circulation owner, allocates platform and landing door IDs in the same exclusive namespace, registers ECS passenger/checkpoint resources, restores cabin and passengers before graph rebuild, and prevents unloading occupied or moving lifts.
- Existing reserved Simulation command envelope handles Call/Board/SelectFloor/Exit. Peer identity still originates in admitted transport; authorization is checked again when queued commands are consumed. Added actual committed lift snapshot export, route and landing access queries, safe actor-retirement gate.
- Lift and aperture changes use one physical scene revision/ACK stream. Native regional nav validates exact actual collider shapes and refreshes affected tiles; route links rebuilt. Pending graph ACKs gate the next gameplay tick. Pure timer updates do not rebuild nav.
- Shared Renderer3D resource owner uploads shell, aperture and lift CPU Models; lift frame copied after graph acknowledgment. Existing graphics barrier owns destruction. No obsolete genre dependencies.

Limitations: authoritative checkpoint recovery, not deterministic lockstep. No real external transport server wiring or GPU/native execution yet; actual game host must supply actor movement, policies and extents. Next: add executable native example with real Simulation and acceptance tests; compile and inspect CI. This stage is source implementation, not a claim of verified release readiness.
