---
name: aiverse-geometry-algorithms
description: Pure integer planar geometry for native games and procedural systems.
---
Use <aiverselib/algorithms/Geometry2D.h>, namespace aiverse::lib::algorithms, AIVERSE::lib_geometry_algorithms. Point2i coordinates bounded to ±1e9 for exact int64 orientation. segmentsIntersect includes touching/collinear overlap. convexHull removes collinear interior points. pointInPolygon uses even-odd rule with explicit boundary. Do not confuse with render Geometry package. No build/tests yet.

triangulatePolygon returns original-index triangles for simple polygons without holes, duplicate closing vertices or collinear consecutive triples (max 4096). simplifyCollinear removes redundant points from open polylines without changing trace. No tests yet.

GeometryClip.h: iterative Douglas–Peucker returns original indices; clipConvexPolygon uses Sutherland–Hodgman for strictly convex input polygons, either winding. Floating point, not lockstep; simplification does not guarantee topology preservation. No tests yet.

Voronoi.h: voronoiCells clips each site's cell to rectangular bounds (max 2048 sites); delaunay uses Bowyer–Watson (max 4096 sites). Duplicate sites rejected. Floating predicates are not robust for near degeneracy or cross-platform lockstep. No tests yet.

Geometry3D.h: rayBox/raySphere return forward parameter intervals; rayPlane and rayTriangle return forward hit parameters, triangle barycentric u/v. Direction need not be normalized. Box/sphere overlaps include touching. closestPoint requires valid box. Floating point, not lockstep. No tests yet.

GeometryDistance.h: closestPoint for segment/triangle (including degenerate triangles), segmentDistanceSquared, capsule-sphere/capsule-capsule overlaps and 15-axis OBB SAT. OBB axes must be orthonormal. Closest point helpers require finite bounded input. Floating point, no lockstep guarantee. No tests yet.

GeometryConvex.h: convexSupport, bounded convexIntersect (GJK, four-state result; tolerance contact), sweepSpheres constant velocity. Indeterminate must not be treated as separation. EPA not implemented. No build/tests.

GeometryEPA.h: convexEPA accepts four actual Minkowski support vertices strictly enclosing origin; returns depth and outward A-B normal on convergence. Caller supplies simplex; current GJK API does not expose it. Invalid/Indeterminate are explicit. No tests.

convexIntersect optionally exports a strict enclosing tetrahedron; convexPenetration automatically invokes EPA when available. Lower-dimensional or early tolerance contacts may yield Indeterminate for penetration even when intersection is known. Witness points remain unimplemented. No tests.

convexPenetrationExpanded attempts a strict tetrahedron from 26 sampled directions on Indeterminate. This is incomplete and can still fail. convexSupportPair returns extreme points, not contact witnesses. No tests.
