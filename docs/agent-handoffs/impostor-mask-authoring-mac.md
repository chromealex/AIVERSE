# Impostor mask authoring

Implementation a1765eee936fa8bf1904e46334de1049a2a24a65 on codex/forest-impostor-contour, planning commit 84f3ce657. The implementation commit includes the Studio screenshot. This stage extends the surface proxy baker, static model importer and Arbor forest adapter.

In Models → Impostor geometry, specify a project-relative rigid model, choose box or plane projection and analyze. Select an atlas view to see its mask and padded rectangle. Full and Trim override Auto. Save to a new GLB. The file contains fitted positions, corresponding UVs, original PBR charts and the recipe in asset.extras.aiverseSurfaceProxy. Binding to a scene LOD remains the existing geometry_proxy_lod operation.

Auto considers each card independently with two texels of border and a minimum 20% reduction in planar area. Empty or dense masks keep their previous full card where applicable. Holes within the bounds still require alpha testing. Arbor combines bark and foliage masks separately across azimuths. Static crossed-card assets can override the default via impostor.fit in the batching sidecar. Existing multi-band tree modes remain explicit experiments. The inspector reports total physical card surface and triangle count; perspective, overlap, depth rejection and GPU time are workload-dependent.

## Verification

The committed CTest report includes static_impostor_lod, terraweave_model, surface_proxy and terraweave_metal_forest (with palm orbit). The surface-proxy checks verify masked texel containment, equal atlas bytes and index count, GLB reload, material bindings, stale preview rejection, output path confinement and existing-file protection. SHA-256 of the complete cooked GLB ties preview to publication. Full paths and commands are in docs/evidence/impostor-mask-authoring-mac.json.

The Studio frontend passed strict TypeScript and the Vite production build. A WebKit component harness sent requests to the built aiversemcp on an isolated project. Plane/Auto gave a 36 × 84 rectangle on each 128 × 128 chart and 5.90625 square units instead of 32. Full gave 32 and Trim gave 5.90625, each with four triangles and one primitive. Switching to the opposite view and saving created masked-source.proxy.glb. Capture: docs/agent-handoffs/media/impostor-mask-studio.png. This is an editor tool; the release/site contract permits Studio capture evidence for this workflow.

The Mac build tree on KINGSTON produced x86_64 binaries. The previous palm and band handoffs' architecture labels have been corrected after inspecting the executables. Their recorded Metal timings are unchanged. Windows, Linux, wind parity, and a merged-tree quick gate were not exercised by this stage. Publication and main integration remain separate.
