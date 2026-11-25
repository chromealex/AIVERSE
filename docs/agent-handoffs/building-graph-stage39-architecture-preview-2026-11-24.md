# Stage 39 — architecture and independent preview

User visual feedback: Underwater / residence / 24x24x18 / seed 482733 in legacy silhouette mode looked like generic armoured building; settings insufficiently differentiated; preview must be inside Building Generator rather than replace Scene; visible detached pieces remain.

Commits:
- 0994efc8e5bb63b086f784e1b8f05538ee7a6c50: replace broad AABB support tests with actual triangle bearing surfaces; preserve portal corridor, correct curved hull station contact and Z origin, mount sloped vent backing and clamp tower dimensions before positioning. Tapered roof regression. Screenshot's precise offending primitives not individually identified; visual confirmation pending.
- a6ea0bc15a64a542a9886267909e2744cab41349: data-driven StructureBody profiles, authored per role for twelve settings. Replace primary armour body with gables, spires, barrel/pressure domes, stepped tiers, pagoda, greenhouse and other profiles; façade panels, remove incompatible copied external industrial forms/props. Original scifi retains legacy grammar. Closed silhouettes retain conservative collision and no claim of navigable interiors. Functional room-shell profile expansion is still pending; existing graph/collision contract unchanged. Tests assert geometry differences and deterministic role generation.
- 48f3eb6d1d96cf6d14a5f76657f785e87b688abe: Building panel owns separate Mac Metal offscreen Renderer3D preview; no Scene replacement or camera mutation. Demand-driven 512x512 GPU frames, orbit on drag release/buttons and zoom, token-scoped teardown. This is not a continuous native embedded viewport; Windows backend unavailable. Real GPU two-angle capture test added to incremental runner.
- e359abcd7b0953044dfc8af000873df42fdf049b: distinct Sci-Fi Space catalog setting and explicit Underwater screenshot recipe fit test.

All new code/tests NOT RUN on Mac by agent. Previous Stage 38 tests passed before these changes only. Run existing KINGSTON incremental verifier, collect full logs, and reopen the native Studio app. Inspect Underwater screenshot recipe, multiple settings/roles, floating attachments and preview alongside an unchanged main Scene. Main untouched. Live owner MCP, complete functional architecture grammar, actual visual quality, continuous preview if needed, non-Mac backend and merged-tree full gate remain open.

Command:
```sh
cd /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c
git pull --ff-only
bash tools/studio/verify_building_integration_mac.sh "$PWD/all-tests-v2"
```
