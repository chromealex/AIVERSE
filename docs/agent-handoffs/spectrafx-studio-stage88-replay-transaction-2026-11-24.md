# Stage 88 — rollback-capable Replay candidate

Parent `57880f166fefac55ecd3557b08d34d4788ca365f`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

The dedicated StudioScene owns an active preview and at most one retained backup. ReplayRestore initializes and prepares a new preview using an inline graph or the exact last successfully loaded graph/options. Setup rejection leaves the original runtime intact. The candidate renders during convergence; previous particles, clock, checkpoints, camera/stage/tween are retained, frozen. On successful convergence the backup is retired; on cancellation or convergence error the candidate is retired and the old state restored. Finalization runs between frames, not in frame provenance. Stop/project switch/destruction discard backup as well. Status includes candidate attempt generation and outcome/error; UI distinguishes rollback from success and updates authored mode only after commit.

Native regressions authored for invalid replay, commit, cancellation restoring generation/tick. No compile, native GPU or UI acceptance run. Candidate resource overlap (two preview runtimes on the same Device) needs hardware verification; no external asset-content fingerprint check or persistent preset history added here. Main unchanged.
