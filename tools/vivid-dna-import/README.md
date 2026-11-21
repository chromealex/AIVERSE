# MetaHuman DNA → VIVID diagnostic import

This **offline authoring adapter** reads real binary MetaHuman DNA using an
externally supplied official Epic DNA Calibration reader. It exports one LOD's
neutral geometry and optionally one exact source blendshape target to a normal
glTF + VIVID JSON manifest. It never treats MetaHuman binary DNA as VIVID JSON.
Neither Epic's SDK nor character data is vendored or linked into the game runtime.

Verified sample: Epic's `data/dna_files/Ada.dna`, upstream revision
`8297ff1bd6f1692a6e221aa8395a66486e5a53c9`, SHA256
`e764cfab938283231bcb3e5af1c7bcbec5154bab4804aafa1b5b7dc6e400ca5e`.
Source: https://github.com/EpicGames/MetaHuman-DNA-Calibration
The sample's internal descriptor says `Archtype`; the filename is Ada.

## Build outside the repository

Supply your separately obtained SDK checkout and licensed character file.
The SDK has its own license; the current general MetaHuman license and the older
Calibration repository license are distinct documents. Do not redistribute the
SDK or sample with AIVERSE. No automatic download or license acceptance is built
into this tool. The pinned reader supports legacy DNA from UE 5.5 and earlier;
UE 5.6+ DNA and arbitrary third-party DNA variants have NOT been verified.

```sh
cmake -S tools/vivid-dna-import -B "$BUILD/import-build" \
  -DDNA_SDK_ROOT="$SDK" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/import-build" -j 8
"$BUILD/import-build/vivid_dna_decode" "$ASSET/Ada.dna" "$BUILD/Ada.json" 0
python3 tools/vivid-dna-import/package.py "$BUILD/Ada.json" "$BUILD/ada-front"
AIVERSE_VIVID_CREATOR_CAPTURE="$BUILD/ada-front.ppm" \
  "$NATIVE/aiverse_vivid_creator" --smoke --still \
  --asset "$BUILD/ada-front/manifest.json"
```

Set BUILD, SDK, ASSET and NATIVE on KINGSTON for this project. A decoder output or
package destination must not already exist. Interrupted decoder output is marked
`.partial`; inspect/remove only that failed output before retrying.

For a second view, package with `--yaw -0.5` (radians). For a source morph, inspect
`meshes[].targets` in the neutral export, then pass its exact name as the decoder's
fourth argument, for example `brow_raiseIn_L`. Package with `--channel browInnerUp`
to explicitly route that *one* target through VIVID's GPU/CPU morph stream. This is
a diagnostic mapping, NOT automatic ARKit or RigLogic semantic equivalence.
Set VIVID `expressionRest[2]` to 1 in that package to exercise this specific brow
channel; `--smoke --still` snaps channels and freezes MotionWeave for comparison.
Creator's jaw/blink/smile demo controls override their own rest channels.

`--weight 1` instead bakes the selected delta into geometry. Combining baked and
live deformation is rejected. Original source position IDs expand to every UV/
normal-layout duplicate, preserving seam consistency. The glTF import order is
the VIVID sparse-morph vertex order, checked on the real Ada test. Source-to-preview
center/scale and baked weights are saved in `provenance.json`.

## Deliberate limits

- This stage exports Y-up reference geometry in a normalized preview frame; it is
  not a production unit/coordinate conversion pipeline. Other coordinate systems
  require inspection before use. Mesh polygons are fan-triangulated.
- No joint hierarchy, skin weights, RigLogic behavior, corrective combinations,
  animated maps, body, groom or complete facial solver is imported yet.
- DNA contains no skin/iris texture maps or groom assets. Diagnostic flat colours
  are used. Untextured eyelashes, eye-edge/tear shells and saliva stay in the asset
  but are made transparent; they require their real opacity/shader assets later.
  The eyelash material tag prevents Creator from adding its procedural wig.
- A source target's position deltas are copied exactly (with uniform preview scale).
  Morph normal/tangent reconstruction and joint-driven expression parity remain.
- The native reader accepts only files within a 512 MiB input cap; exported LODs
  are capped at one million layout vertices. This is not a hardened untrusted-file
  service or a compatibility claim for every DNA revision.

## Actual acceptance, 2026-11-20

Apple Silicon SDK/adapter and Creator builds passed. Ada LOD0: 9 meshes, 34625
layout vertices; 681 source target names on the head. Four package tests passed
(UV seam expansion, invalid index, no double deformation, output preservation).
Reader rejected invalid DNA signature, out-of-range LOD and `0junk` LOD.

Actual Metal captures passed for front, ¾, live brow GPU and live brow CPU. The
live source target changes 200 viewport pixels at total RGB threshold 4 versus
neutral. GPU vs CPU differs at **one** of 586500 sampled pixels (max RGB sum delta
8); strict bit-exact image equality FAILED and is not claimed. Both paths rendered
successfully. Full RigLogic, texture quality, newer DNA, and other backends remain
NOT TESTED. All artifacts/logs are under the external build root's `metahuman/`.

## Full-character companion export (2026-11-20)

The legacy Epic `Ada.dna` sample contains the head/neck, not a full body. A full
character cannot be reconstructed from that file alone. For full-body acceptance
we separately imported the published Ada UE 5.6 GLB from
https://github.com/creategamecharacters/metahuman-to-glb (CreateGameCharacters).
Source URL: https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/ada.glb
SHA256: `70ad5d190e3cdfb54e88d405b75d7b4731547f2bd93d5cbad0ce4720946d99de`.
This is a companion-export test, NOT evidence that the binary DNA adapter imports
body DNA, clothing, original RigLogic or skinning from the old head-only sample.

`prepare_full_character.cjs` uses external packages `@gltf-transform/core@4.2.1`,
`@gltf-transform/extensions@4.2.1`, `draco3dgltf@1.5.7` installed into a separate
KINGSTON tooling directory. It decodes Draco offline because the native importer
requires uncompressed geometry. The 7 meshes, 15 primitives, 5 skin palettes and
16 texture images are retained; duplicate garment bone names are made unique.
The face skin's own head bone becomes `vivid_face_head`. A parent frame uniformly
scales/centers the model for Creator without rewriting skin inverse-bind matrices.

```sh
node tools/vivid-dna-import/prepare_full_character.cjs \
  "$TOOLING" "$BUILD/ada-source.glb" "$BUILD/ada-full" 0 "$BUILD/mh_materials.json"
AIVERSE_VIVID_CREATOR_CAPTURE="$BUILD/ada-full.ppm" \
  "$NATIVE/aiverse_vivid_creator" --smoke --still --asset "$BUILD/ada-full/manifest.json"
```

Use yaw `-0.5` for the ¾ view. Destination must be new. `mh_materials.json` comes
from the same published character directory. The source UE cloth material uses a
packed region mask in the GLB base-colour slot: the adapter removes that colour
binding and applies the author's `diffuse_color_1`, roughness and metallic values.
This is an explicit **uniform-cloth fallback**, not full mask/detail shader parity.
Original skin and other texture bindings stay in the asset. Hair uses VIVID's
material profile; exact UE hair/eye appearance has not been established.

Creator idle animation now adds its demo bob to the head's bind translation:
absolute zero translation would collapse a real imported head toward its neck.
Actual Metal front, ¾ and CPU captures all passed. Both final GPU images were
visually inspected, including hands, feet, clothes, head and hair. Native source
has 119916 position vertices; after importer skin-palette splitting Creator shows
125302 vertices. The full body is in its rest pose; complete rig animation and
original facial solver parity have NOT been tested.

All assets, package-lock, builds, provenance and captures are external under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/`.
No character asset or external dependency is vendored in the repository.

## Companion texture conversion (2026-11-20)

Pass a final `source-maps` directory to `prepare_full_character.cjs` after the
material JSON. It must contain the published `textures/*.png` paths from that
JSON. Also install external `sharp@0.34.5` in TOOLING (on KINGSTON). With this
argument the former uniform-cloth fallback is replaced by `bake_materials.cjs`:

- Cloth: source mask channel blends authored linear colours, available textile
  detail/macro maps and AO. High-frequency detail is prefiltered for the bake's
  texel footprint to avoid moiré. The result is encoded to an sRGB colour texture.
- Hair, eyebrows and eyelashes: source coverage is packed into alpha. Hair R/G
  now store sRGB-encoded root position/strand seed for the appearance recipe;
  lash RGB remains neutral. Eyelashes use BLEND with
  no depth write. Hair/brows use a BLEND fringe plus a MASK core (cutoff 0.5),
  sharing glTF accessors/textures. Native import currently duplicates core
  vertices, so this adds geometry memory and draws. The preview groom uses a
  0.55 roughness floor, following the companion viewer, to reduce pin glints.
  The former universal 0.35 cutoff discarded soft strand coverage.
- Eyes: the original iris atlas R/B channels, per-eye UV pole, iris/limbus/pupil
  ramps and authored colours are baked in original mesh UV. The atlas transform
  is removed from the resulting texture to avoid applying it twice.
- Skin cavity and available teeth base-colour/normal/mouth-occlusion maps are
  attached from the companion files. Original 16 GLB image payloads, including
  skin colour, normal and roughness maps, are retained byte-for-byte.

The generated package has 29 images and 13 converted/attached material records in
provenance.json. Original assets and source-map hashes remain outside the repo.
This is deterministic material data conversion, not AI-generated character art.

VIVID high/medium/low hair graphs now preserve sampled input alpha with setAlpha;
previously procedural RGB also replaced alpha and could fill whole hair cards in
colour/shadow passes. `aiverse_vivid_creator --smoke --still --portrait --asset ...`
adds a close camera preset for this normalized full-character package. Default
full-body framing is unchanged.

Tests: `node test_bake_materials.cjs "$TOOLING" "$TMPDIR"` checks red-channel
fractional coverage against deliberately different source alpha, blended lashes,
shared masked-core accessors/textures, linear/sRGB cloth colours,
colour roundtrip and path escape rejection. Native vivid_human_appearance passes
all material quality budgets after the alpha fix. Real Metal full/portrait captures
are recorded in the feature handoff. Cloth/iris baking remains an approximation:
no view-dependent eye refraction, procedural sclera veins, tangent-atlas hair
anisotropy or complete UE skin/scattering parity is claimed.


## Imported appearance profiles

New packages include `appearance.json` and an optional manifest `appearance`
relative path. The recipe is separate from binary MetaHuman DNA and VIVID DNA.
72 bounded controls cover skin texture/tone/freckles/roughness, hair pigment,
melanin/redness/gray/ombre/highlights/coverage, eyes/limbus/pupil/sclera,
fabric finish/normal/tint, and 10 presentation body proportions. Material roles
are explicit; unknown materials reject before publication. Creator reads the
manifest profile automatically, or accepts `--appearance profile.json`.

The shared PBR shader graph overrides roughness explicitly, so imported packed
maps cannot silently reduce the chosen matte finish. No extra fixed Skin/Eye/Hair
highlight lobe is applied to these imported materials. This is a bounded portable
approximation, not Epic's skin scattering, anisotropic hair or corneal refraction.
Facial texture libraries and authored wrinkle/mole masks are not bundled.

Body shaping is a Y-up post-animation field over skin joints, including separate
clothing rigs. It does not alter source skeletons, gameplay, physics or DNA. It
requires a full skinned character; extreme shapes need garment collision checks.


### Eyelash coverage correction — 2026-11-20

A coverage PNG may contain white RGB and the actual mask in alpha. The baker
now infers A for that unambiguous eyelash layout when alpha_channel is omitted;
explicit channel metadata still wins, hair retains its R default. The Ada source
Eyelashes_L_SlightCurl_Coverage.png has all-white RGB and 935962 transparent alpha
texels, which the former R default converted to an opaque rectangle. Regression
fixtures cover both red-mask and white-RGB/alpha-mask exports. Existing packages
must be rebaked or their affected texture replaced; changing code alone cannot
repair already baked pixels.

### Bind groom cards to the animated head — 2026-11-20

The Ada companion has two unskinned card nodes (hair and eyebrows) initially
parented to the character root. Their static placement is correct, but they do
not follow head animation. Preparation now calls `attach_grooms.cjs` after framing:
rigid Hair_/Eyebrows_ nodes attach to the mapped facial head. An exact inverse-TRS
chain preserves their world bind frame, including nonuniform scale without shear
loss. Meshes, materials, alpha, node weights and morph channels are not duplicated.
Already-descendant nodes are unchanged; skinned meshes are rejected by this helper
and must use a skeletal attachment path. `groomBindings` records provenance.

The helper is an offline card attachment, not a UE strand-groom binding asset or
scalp barycentric deformation. Brows retain their named face morphs (46 on Ada);
lashes already belong to the skinned facial mesh. All use the existing animation
pose; no copied animation or independent clock is introduced.

Test: `node tools/vivid-dna-import/test_attach_grooms.cjs <KINGSTON tooling dir>`.
Checks bind preservation, rotation/nonuniform scale following, morph ownership,
idempotence, cycle/skinned rejection and GLB hierarchy roundtrip.

### Explicit clothing repair fixtures

`smooth_clothing_weights.cjs TOOLING INPUT MATERIAL NEW.glb [ITERATIONS] [STRENGTH]`
provides an offline weight-smoothing fallback for authored clothing lacking pose
correctives. It changes only the selected material's four skin influences and
rejects morphing cloth. Defaults4/.65; maximum16iterations. It does not simulate
cloth or replace authored Unreal pose correctives. Keep the original asset.

`mask_covered_skin.cjs TOOLING INPUT MASK.json NEW.glb` bakes an outfit-specific
visibility mask into indices. Schema: `{version:1,aboveY:number,materials:string[],
neckBand?:{minY:number,halfWidth:number,requireNeckWeight?:boolean}}`. Coordinates
are bind-world; lower-neck band is centered on world X0. Hands are retained by
hand-joint ancestry. This is intended for an explicitly closed upper-body outfit,
not a general clothes detector. Rebuild from original for another outfit, and
regenerate external vertex-index/LOD sidecars after import topology changes.
