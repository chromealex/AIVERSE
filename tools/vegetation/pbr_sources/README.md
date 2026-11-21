# Source provenance

Generated using OpenAI built-in image generation on 2026-11-20, in this task. Original PNGs are preserved here.
- oak_bark_basecolor: realistic mature oak bark, seamless full-frame orthographic unlit basecolor, muted grey/brown, no directional shadows/highlights.
- oak_bark_height: reference edit of oak_bark_basecolor requesting spatially aligned grayscale relief with dark fissures and raised light platelets; approximated, not measured.
- oak_leaves_basecolor: four isolated oak sprigs, 2x2 transparent atlas, lobed leaves with fine veins, flat neutral illumination, no scene/background/text.
- shrub_leaves_basecolor: four isolated myrtle-like shrub sprigs, 2x2 transparent atlas, small oval muted sage/olive leaves, neutral illumination, no scene/background/text.

2026-11-20 expansion: pine_bark, birch_bark and spruce_bark are generated
aligned basecolor/height pairs; full original `*_source_pair.png` files are
preserved alongside extracted halves. Pine and spruce foliage are separate
transparent 2x2 branchlet atlases generated with bottom-centred stem anchors.
PBR roughness/AO/height remain authored approximations, not measured scans.


## Botanical leaves,2026-11-21

`birch_leaves_basecolor.png`, `maple_leaves_basecolor.png`,
`apple_leaves_basecolor.png`, `willow_leaves_basecolor.png` were generated with
the built-in imagegen tool and visually inspected as2x2 transparent sprig atlases.
No external photographic scan/SpeedTree asset was copied. The standard shared
baker emits1024px albedo/opacity/normal/ORM/roughness/AO/metallic/height and
optional translucency maps. Relief is a small authored albedo-detail estimate,
not measured leaf thickness or a scan; optional translucency is not consumed by
standard PBR. Original generated alpha is preserved by normal asset resampling.

Prompt set: photorealistic production PBR game foliage albedo atlas; four separate
species-correct twig variants in an exact2x2 arrangement; every twig starts at
local bottom-centre(u.5,v.97), top aroundv.08,6% safety margin;18..28 leaves with
natural overlapping density and tilted faces; flat neutral diffuse albedo,
muted green, crisp botanical veins; opaque leaves/wood with fully transparent
interior gaps, alpha255/0 and narrow antialias only; no cloudy matte, baked shadows,
rim light, text/grid/watermark/fruit/flowers. Species: Betula pendula triangular
serrated leaves; Acer platanoides five pointed palmate lobes; Malus domestica
elliptical serrated blades; Salix babylonica slender hanging lanceolate leaves.
Birch first attempt had cloudy alpha; a built-in edit explicitly removed every
matte/background gap and halo while preserving twig layout and leaf shapes.
The cleaned variant is the source committed here; first draft discarded.
