# VIVID wardrobe and character assembly

In native Studio open **VIVID Appearance**, select the source character and load
its appearance profile. Edit the existing skin/hair/eye/cloth/body controls.
The **Одежда и сборка персонажа** section loads real mesh/material/bone names.

- Toggle source meshes to remove old clothing. Each wardrobe item can also hide
  source meshes only while that item is enabled.
- Add clothing, watches, chains or other accessories from project model assets.
  Select a slot; only one enabled item per slot is accepted.
- Skinned clothes require exact, unique character bone names and matching world
  bind transforms. They use the character skeleton and animations. Independent
  garment clips and socket offsets on skinned clothing are rejected.
- Rigid accessories attach to a selected bone, with local translation, rotation
  in radians and positive XYZ scale. Translation is in the bone's coordinate
  units, which may differ from metres in imported skeletons.
- Choose **Ткань** to bake the profile's cloth controls, or **Исходный PBR** to
  retain authored metal/roughness on jewellery.
- Add a body material and an imported texture as a visibility mask. Red-channel
  black hides, white retains visibility. Masks share the material UV transform;
  several active masks multiply together and multiply the existing alpha.
- **Собрать персонажа** starts an asynchronous native build. Choose a fresh name
  and a maximum baked colour-texture edge (256–4096). **Показать собранного
  персонажа** loads the completed asset through the existing native GPU owner.
  Wardrobe edits are drafts until assembly; there is no live garment preview.

The output is `content/Characters/<name>/model.glb`, with embedded PNG textures,
PBR factors, baked body bind geometry, updated joint pivots/inverse binds,
original character clips and facial morph names/deltas. Masked triangles remain
alpha-cut geometry; hidden source meshes are removed. The package contains
multiple skin/material primitives in one asset, **not one draw call**. Unused
materials/images are removed. This stage assembles the selected base meshes;
LOD generation, atlas packing and physics are separate work.

`source.json` retains the source selection, wardrobe and appearance recipe;
`appearance.json` is an empty live profile so the output is not adjusted twice.
Sources are never overwritten. The native writer uses exclusive staging and
no-replace atomic publication on macOS, after reimporting and checking the GLB.
The output must fit the native 128 MiB model-file budget. Source references in a
Studio recipe use project GUIDs; the trusted worker resolves them to content keys.

An optional sibling `wardrobe.json` is a portable preset: asset/mask references
are **content-relative paths**. “Загрузить гардероб модели” resolves them to GUIDs.
Schema: `{version:1,hiddenMeshes:[],items:[{id,asset,slot,enabled,bone,
translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1],finish:"cloth"|"source",
hideMeshes:[],masks:[{material,texture}]}]}`. Limits: 32 items, 16 masks per item.

Body baking uses the existing upright-character deformation field; translated
animation keys and bind pivots are adapted, not anatomically retargeted. Check
extreme proportions and animated garment intersections. Procedural appearance
noise may differ slightly between CPU and GPU floating-point implementations.
Bake texture resolution controls fine detail/coverage fidelity. Legacy Creator
sculpt drafts or an unsaved facial-preview rig are not inputs to this builder.

Headless verification uses the same native publisher:
`aiverse_vivid_assemble PROJECT SOURCE_REL WARDROBE_JSON LOOK_JSON NAME RESOLUTION`.
The CLI recipe uses content-relative paths. `wardrobe_fixture.py` creates original
procedural accessories and a shirt variant for the external prepared Ada sample;
it is test tooling, not a general garment-fitting system.

Baked Creator test manifests can set `authoredHair:true` to prevent a second
procedural groom without replacing the baked PBR materials. The optional v1
field defaults to false; older packages keep their behaviour.
