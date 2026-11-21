# Explicit Terra environment entities

New MapDoc documents use `terrainPresentation: "entities"`. They contain no
implicitly rendered terrain, default water or fallback floor. Old documents without
this key retain their legacy terrain adapter. Projects may additionally set
`terrain.render: false` in rules/game JSON. These are presentation switches; they
are not a completed migration of legacy gameplay terrain/collision authority.

Terrain tiles, trees, rocks and grass objects belong to the document's existing
`entities` array. Never replace the entire world with one packed model entity.
Example (metres, degrees):

```json
{"id":"tree-0042","type":"pine_prototype_2","pos":[12,30],
 "components":{"Transform":{"y":4.5,"yaw":128,"scale":0.9},
 "StaticModel":{"asset":"content/models/pine-2.gltf"},
 "TerraTree":{"prototype":2}}}
```

Canonical X/Z remain `pos` for MapDoc compatibility; canonical Y/yaw/uniform scale
are in Transform. Do not introduce a second mutable XYZ copy. The editor composes
existing LocalTransform/affine math into a render matrix. StaticModel references a
shared asset, never a per-instance copied mesh. Native metadata records provenance,
not a second live Terra simulation. Existing unknown JSON fields round-trip.

Studio HTML5 identifies these objects as `entity:@<id>`; legacy objects keep their
ordinal protocol. Deleting an earlier array entry does not change authored IDs.
Hierarchy, Inspector, selection, duplicate, Undo and save use the same document.
The model asset is read-only in Inspector for this stage. The legacy RtsView host
uses generated per-prototype rules bindings matching StaticModel; those bindings
are a compatibility adapter, not another editable asset owner.

NEREID water is an entity with Transform and a NereidWater surface component.
Geometry/grade origin are local-space; the read-only native adapter translates
points, body height/centre, grade origin and vortices. Flow edits commit to the
component with MapDoc Undo. Legacy top-level nereidWater remains supported.
Water entities currently support translation only; rotation and non-unit scale
are rejected explicitly. The Studio Hierarchy selects the water entity; precise
water-surface picking is not implemented.

Rigid model LOD/masked foliage can render in Studio. CPU clicking uses base mesh
geometry, so alpha holes and alternate LOD silhouettes are approximate until GPU
ID/depth picking exists. Current Studio preview caps are2000 object proxies and512
shared assets; these are authoring limits, not a streaming/world-scale claim.

The AlpineValley conversion contains1391 document entities:48 terrain tiles,
967 trees,221 rocks,154 grass patches and1 river. Each grass patch contains tufts;
individual tufts/blades are NOT separate document entities yet. Fully independent
per-tuft authoring, runtime ECS adoption, live terrain editing/ecology, gameplay
collision/buoyancy and production-scale picking/streaming remain separate work.
