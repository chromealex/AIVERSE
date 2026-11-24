# Stage 58 — guarded save caller audit

Branch feature/aiv20261124_graph_owner_inspection, PR #183. Parent 22ada021bb719b5b6234546a1b67ab67b2a97c30; code commit cba7a38ed382ce8a1eeb04e7d893654b5d3b80b4.

Default-branch code search for shader_graph_set, fx_graph_set, fx_graph_draft_set, fx_graph_draft_publish and studio.shaderGraphSave identified candidate callers; fetched relevant files from the actual integration ref. Native shader and FX windows and visual graph panels already pass original documents. SpectraFunctionOps reference is comment-only. Existing direct graph test writes create new assets or intentionally reject invalid documents.

ToolPanel.tsx now passes expectedProject for generic operation cards and read-state requests, and treats a returned ok:false as a failed operation. Added native StudioSession fixture for missing original, wrong project, valid guarded save, stale original refusal and on-disk content. Updated shader and SpectraFX docs in both available languages, both skills and graph-framework manifest. No format or API weakening.

Search index is default-branch only, so this is a scoped audit, not exhaustive grep over current merged checkout. Native Caverns owner at 804ac9be0b3c8c3c1f9177c052cdaf07c3a3879b remains unmerged, as does latest Building advancement 41975c21320664e872e10ffd35c829c9c7b6992f. No build, typecheck, CTest, Mac or live MCP QA run. No main/owner refs changed. Next: actual full three-way Caverns integration and full-tree caller audit before acceptance.
