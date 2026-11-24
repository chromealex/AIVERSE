# Caverns Stage54l — planetary local-frame foundation

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and source feature branches unchanged.

## Source commits

- f4ead19f / d841645a: bounded right-handed Fixed chart, stable body/face anchor, separate simulation and orbit positions.
- 10c11d5e / 43d1610d / 3b256294 / 74d83d27 / 610d0e77 / c0038824: opt-in planetary region resource, bounded archive/JSON anchor, load and canonical stamp validation.
- 367e027d / a09b19ec / 52672e7d: reuse existing indexed SDF mesher/raycast and navigation provider; PHYSIS gravity at each ground sample, chart/residency constraints.
- 72bd4539: project actual selected coarse cube-sphere surface triangles, sample triangle height and derive local shaft mouth.
- 26509899 / f45245f4: prepare common transformed Model and collision payload with explicit simulation anchor versus orbit origin.
- 18840003: existing PHYSIS Box3D bridge wrapper with planetary region validation.
- 7648abea / b264e586: skills.

## Contract

Use one immutable game::Surface and authored body ID, face, Q16 face coordinate and chart half extent. Frame origin is on the reference radius, unaffected by surface height changes; chart Y points radially out at anchor, east and south form right-handed basis. Chart extent <= body radius/8, max existing Surface radius 1500m; charts are local and cannot be silently continued to other bodies or across distant patches. Fixed rotations are quantized, not proven exact inverse round trips. Choose resolution/spacing so entire chunk and gradient halo fit the chart; default 16m lattice can be too large. Planet region persistence is opt-in and flat schemas remain unchanged. Register after existing cave authority and before seal; authored volume recipe and stamp influences must fit chart. The dirty halo/mesh boundary and restored live Surface identity still need host validation.

Surface adapter captures explicit coarse simulation cube-sphere cells (including multiple faces) with existing triangle diagonal and outward winding, transforms to local chart, samples actual triangle planes. No synthetic flat terrain. Does not yet cut native spherical terrain, account for visual detail/LOD, or prove transformed rim watertightness. Native payload uses shared exact triangle conversion into existing Model/SpatialWorld/Box3D preparation; world body anchor and render orbit position separated. Callers must scope owners by chart/body and actually publish/retire resources. No automatic planet renderer/physics installation. Gravity uses existing PHYSIS fields at simulation positions; author matching radial field rather than relying on default global -Y. Current ground path provider operates in chart centimetres and refuses gravity whose local up dot Y <0.99, zero or reversed. This is not arbitrary-up pathfinding. Use existing applyPhysisToBody only once per dynamic body step; new wrapper does not add a second force. Native compile, CTest, frontend build, physics/GPU runtime tests NOT RUN.

## Next Stage54m

Cut actual selected planet mesh triangles in chart coordinates and map cut fragments/rims back to shared native surface owner. Preserve body/face identities and existing detail/LOD boundaries. Connect chart-specific volume and surface ownership to planet renderer, native physics and nav; ensure no duplicate old surface draw or colliders, handle chart seams and Studio authoring. Then implement arbitrary gravity-oriented ground support and actual integration; mandatory KINGSTON QA before main. Feature incomplete.
