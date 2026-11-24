# Stage 107 — live GRANULAR diagnostic section geometry

Base 0a82aff3. In progress. Existing Field has no physical placement scale in ECS or authored grid; the optional native PlanetGranularPlacement owner adapter requires explicit cell size, depth and clearance. Do not invent physical metadata. Provide explicit transient Studio diagnostic square-cell scale (off by default; user-selected centimetres, depth equals cell size, zero ground clearance) through existing Live toggle/typed bridge; use canonical planetGranularTops and actual ECS completed tick. Bounded occupied tile traversal and quad count, body-local output, selected-body Scene overlay, clear limitations. Keep native physical owner/persistence untouched, no terrain/collider writes. Tests deferred; no main merge.

Status: source implementation 947b3a49a7312d385f170fa64f7b0a296c6a3967. Test/build/visual acceptance NOT RUN. Physical owner placement and terrain mutation remain separate; next live EVOLVE.
