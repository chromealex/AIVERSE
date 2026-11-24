---
name: aiverse-swept-shapes
description: Use the swept-shapes Fixed-point helpers in AIVERSE simulation code - will a moving car hit a crossing pedestrian within a horizon, slide a walker along walls and furniture, cheap polyline bounds before an exact query - instead of ad-hoc geometry.
---

# swept-shapes

Header: `engine/core/SweptShapes.h` (`AIVERSE::lib_swept_shapes`). Tests: `tests/libraries/city_tests.cpp`.

1. Crossing danger: `sweptBoxCircle(at, velocity, forward, halfLength, halfWidth, radius, horizon)` with the circle's position and velocity relative to the box (car) and the box's unit forward axis; `hit` plus the entry / exit window. Put the time the walker needs to clear the lane into `horizon`.
2. Walkers inside rooms / yards: build `AxisBox`es (`axisBoxMilli` for millimetre data), grow them by the walker radius with `inflate`, then `slideMove(x, z, tx, tz, boxes, ox, oz)` clamps the move axis by axis (x first) so walkers slide along walls.
3. Broad phase: `polylineBounds<Vec2 or Vec3>(points, margin)` and `mayTouch(bounds, x, z, radius)`; an empty polyline touches everything. Keep the exact test after it.
4. Fixed only, no state: safe in deterministic systems; iterate boxes in a stable order.
