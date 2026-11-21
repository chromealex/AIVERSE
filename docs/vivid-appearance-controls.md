# VIVID appearance profiles

Open **Panels → VIVID Appearance** in native Studio. Pick a prepared character,
choose **Load model profile**, then **Apply to preview**. The selected model must
live inside the opened project's `content/` directory, with `appearance.json`
beside its glTF. You can also import a recipe JSON, edit material roles, and save
named looks to `human-looks/`. Browser code never writes project files directly.

All controls below affect the native imported-model path. Changes are a draft
until Apply; Undo/Redo operate on the draft. Apply preserves a bound MotionWeave
facial rig when editing the same model, and never creates a second Animator.
New materials publish through the existing validated CPU/GPU resource owner.
A failed import retains the last model; a rejected draft cannot set GPU readiness.

## Finish correction

The companion shirt's original roughness was 0.30; skin's packed map could push
roughness near zero. Profile defaults set fabric to 0.88 and skin to 0.62 (before
oil/wetness), with reduced normal strength. The graph explicitly controls final
roughness instead of multiplying it by the old low-valued map. Coverage remains
sampled for hair and lashes. PBR is used without an extra fixed human glint lobe.

## Controls

RGB controls use linear colour values. Body values are relative proportions,
not centimetres or clinical measurements. Freckles and pore colour detail are
procedural UV approximations, not original MetaHuman texture-index presets.

| Group / key | Control | Range | Default |
|---|---|---|---|
| `skin.roughness` | Skin roughness | .15–1 | .62 |
| `skin.oiliness` | Oiliness | 0–1 | .06 |
| `skin.wetness` | Wetness | 0–1 | 0 |
| `skin.normal` | Normal texture strength | 0–2 | .65 |
| `skin.cavity` | Cavity strength | 0–1 | .8 |
| `skin.texture` | Colour texture strength | 0–1 | 1 |
| `skin.exposure` | Skin exposure (stops) | -2–2 | 0 |
| `skin.saturation` | Skin saturation | 0–2 | 1 |
| `skin.contrast` | Skin contrast | .5–1.5 | 1 |
| `skin.redness` | Skin redness | 0–1 | 0 |
| `skin.tintR` | Skin tint red | 0–1 | 1 |
| `skin.tintG` | Skin tint green | 0–1 | 1 |
| `skin.tintB` | Skin tint blue | 0–1 | 1 |
| `skin.freckles` | Freckle strength | 0–1 | 0 |
| `skin.freckleDensity` | Freckle density | 0–1 | .35 |
| `skin.freckleScale` | Freckle frequency | 40–300 | 140 |
| `skin.freckleTone` | Freckle darkness | 0–1 | .5 |
| `skin.pores` | Pore colour detail | 0–1 | .1 |
| `skin.poreScale` | Pore frequency | 20–300 | 150 |
| `hair.roughness` | Hair roughness | .15–1 | .65 |
| `hair.melanin` | Melanin | 0–1 | .72 |
| `hair.redness` | Hair redness | 0–1 | .12 |
| `hair.dyeR` | Dye red | 0–1 | 1 |
| `hair.dyeG` | Dye green | 0–1 | 1 |
| `hair.dyeB` | Dye blue | 0–1 | 1 |
| `hair.gray` | Gray strand fraction | 0–1 | 0 |
| `hair.grayLightness` | Gray lightness | 0–1 | .65 |
| `hair.variation` | Strand colour variation | 0–1 | .25 |
| `hair.rootDarkening` | Root darkening | 0–1 | .15 |
| `hair.ombre` | Ombre intensity | 0–1 | 0 |
| `hair.ombreStart` | Ombre start | 0–1 | .55 |
| `hair.ombreSoftness` | Ombre softness | .02–.5 | .2 |
| `hair.tipR` | Tip red | 0–1 | .5 |
| `hair.tipG` | Tip green | 0–1 | .2 |
| `hair.tipB` | Tip blue | 0–1 | .08 |
| `hair.highlights` | Highlight strand fraction | 0–1 | 0 |
| `hair.highlightStrength` | Highlight intensity | 0–1 | .5 |
| `hair.coverage` | Strand coverage | .25–1.5 | 1 |
| `hair.cutoff` | Dense core threshold | .1–.9 | .5 |
| `lashes.coverage` | Eyelash coverage | 0–1.5 | 1 |
| `lashes.roughness` | Eyelash roughness | .15–1 | .75 |
| `eyes.roughness` | Eye roughness | .08–.65 | .25 |
| `eyes.irisSize` | Iris radius (UV) | .08–.19 | .14 |
| `eyes.pupilSize` | Pupil radius (UV) | .008–.075 | .033 |
| `eyes.irisR` | Iris red | 0–1 | .176 |
| `eyes.irisG` | Iris green | 0–1 | .077 |
| `eyes.irisB` | Iris blue | 0–1 | .045 |
| `eyes.irisTexture` | Iris texture detail | 0–1 | .7 |
| `eyes.limbus` | Limbus darkness | 0–1 | .35 |
| `eyes.limbusWidth` | Limbus width | .002–.04 | .015 |
| `eyes.scleraLightness` | Sclera lightness | .2–1 | .88 |
| `eyes.redness` | Sclera redness | 0–1 | .08 |
| `eyes.leftU` | Left iris centre U | 0–1 | .488389 |
| `eyes.rightU` | Right iris centre U | 0–1 | .50877 |
| `eyes.centerV` | Iris centre V | 0–1 | .504 |
| `cloth.roughness` | Fabric roughness | .3–1 | .88 |
| `cloth.normal` | Fabric normal strength | 0–2 | .4 |
| `cloth.tintR` | Fabric tint red | 0–1 | 1 |
| `cloth.tintG` | Fabric tint green | 0–1 | 1 |
| `cloth.tintB` | Fabric tint blue | 0–1 | 1 |
| `cloth.saturation` | Fabric saturation | 0–2 | 1 |
| `cloth.exposure` | Fabric exposure (stops) | -2–2 | 0 |
| `body.height` | Height scale | .8–1.2 | 1 |
| `body.width` | Body width | .8–1.2 | 1 |
| `body.depth` | Body depth | .8–1.2 | 1 |
| `body.shoulders` | Shoulder width | .8–1.2 | 1 |
| `body.chest` | Chest width | .8–1.2 | 1 |
| `body.waist` | Waist width | .75–1.25 | 1 |
| `body.hips` | Hip width | .8–1.2 | 1 |
| `body.head` | Head scale | .85–1.15 | 1 |
| `body.legs` | Leg length | .85–1.15 | 1 |
| `body.torso` | Torso length | .85–1.15 | 1 |

## Source textures and limits

The model keeps its source colour, normal, cavity/AO and alpha textures. Skin
texture strength, normal strength, tone, saturation, contrast and exposure are
editable; changing texture files still uses the asset pipeline. There is no
bundled MetaHuman face/body texture library or authored regional accent masks.

Prepared hair R/G contain sRGB-encoded root position / per-strand seed and A is
coverage. Colour effects require this convention; do not assign an arbitrary
albedo texture to the hair role. Eyelashes keep white RGB plus source coverage.
Ombre/highlights/gray colouring does not generate a new haircut. Hair strand
geometry, length/curl grooming, anisotropy and order-independent transparency
remain separate work. The original procedural-groom editor remains available.

Eye masks use the profile's per-eye UV centres. These must match the selected
model. Pupil and iris sizes adjust the rendered appearance, without modelling
corneal refraction. Original UE shader parity is not claimed.

Body shaping operates on an upright Y-up full-body skin-joint pose after
animation, and on rigid attachments such as hair at their mesh centres. It
includes duplicate clothing skeletons. The Animator, bind skeleton, collision,
gameplay and source mesh remain unchanged. This is a bounded presentation
approximation; extreme proportions and animation need garment-intersection and
culling checks, and are not a substitute for authored anatomical morph targets.

## References

Parameter families were informed by Epic's [hair overrides](https://dev.epicgames.com/documentation/metahuman/hair-overrides)
and [skin material tools](https://dev.epicgames.com/documentation/metahuman/skin-material-tools).
This implements a portable subset plus AIVERSE controls, not the full MetaHuman
Creator parameter/texture library.

## Verification status

Native material/body tests, profile persistence/validation and actual Metal
frames are recorded in the feature handoff. The Mac remained locked; the user
explicitly requested continuing without UI interaction. Panel interaction,
240px/desktop WKWebView screenshots and animation-wide deformation QA are NOT
VERIFIED. Render screenshots are from real Creator Metal captures, not a mock UI.
