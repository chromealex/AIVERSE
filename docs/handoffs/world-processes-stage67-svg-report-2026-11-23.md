# Stage67 — read-only SVG ecology visualization
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
2d555649 adds --svg FILE to EvolveForestDemo.cpp. Draws authoritative half-open ECS rectangles colored by fertility/pollution, and rooted plants with dead/fallen markers from existing TerraWeave resources. Fixed viewport is intended for the three-region demo, not arbitrary maps. No RHI or live Studio window added.
Example: aiverse_evolve_forest_demo examples/native/world_processes/evolve-forest-demo.json 1024 --svg ecology.svg
No compile or run; no SVG output verified. Next: source integration audit and comprehensive acceptance when authorized.
