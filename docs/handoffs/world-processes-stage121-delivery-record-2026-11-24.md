# Stage 121 — GRANULAR physical placement and body-local publication

Base 8b518453. In progress. Reuse existing PlanetProcessGeometry and optional native GranularBox3DPublisher/GranularRenderPublisher. Validate explicit cell/depth/clearance scale against canonical body and actual Transform; derive identical fixed coordinate origin and quad geometry for both owners. Add explicit single-body local frame option to avoid artificial simulation anchors in physical/render scenes, preserve existing canonical simulation-space option and planar adapter. Reject mixed bodies in local frame; keep PHYSIS authority unchanged. Add native source fixture. No automatic global renderer/physics activation or TerraWeave mutation; tests deferred, no main merge.

Status: source 933567e9ac3bbe2228b87ebc859c931755749a9c; fixture NOT RUN; automatic host/authoring and real physics acceptance remain open.
