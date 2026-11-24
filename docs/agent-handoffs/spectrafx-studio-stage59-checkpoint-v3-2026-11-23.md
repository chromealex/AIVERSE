# Stage 59 — SPFX V3 RNG metadata

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 42abcb2f61e3fa2cf20904a8ef0cf23aaccfff18 and 507f74d119cb54072abc3fa88e2a953c6171aace.

SpectraCheckpointExport.h adds V3 12-byte RNG trailer, CRC coverage, bounds checks and metadata decoder; V2 unseeded output preserved. SpectraCheckpointInspector.h accepts V2/V3 and exposes RNG metadata. Skill updated. No tests or builds. Existing external V2-only readers need audit. This remains diagnostic only; live GPU restoration requires full allocation/identity/parameter state and checked render-owner upload, not just seed assignment. Next implement safe restore contract and update external diagnostic readers.
