# Projectile segments

Choose one of four scenes (or open index.html?map=0 through map=3): straight, arc, ricochet and timed split. The launcher fires automatically at a nearby target; select it and right-click another target to redirect fire. Escape opens Pause; Restart resets the scene and Main menu chooses another scene. Rotate/zoom the camera to inspect the imported green diamond and trail.

All scenes use ordinary weapon flight data, the shared projectile presentation sample and the existing trail binding. No demo-specific engine code. Source presets derive from the native Metal acceptance fixtures retained under docs/evidence/issues-projectile-metal-20261127. All four scenes also load in WebGL; retained browser frames show ricochet and split. Native frames show straight and arc flight more clearly. Publication and final integration remain pending.
