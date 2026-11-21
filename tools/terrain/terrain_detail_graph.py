"""Bounded native Shader Graph for continuous Terra PBR detail layers.

Four shared 1024px materials cover 4m each; the tile texture only owns weights.
No height generation, renderer fork or surface transparency.
"""
import math
import json


def material_graph(weights_path, atlas_paths, metres=4.0, transparency=0.12,
                   noise_underlay=True, resolution=1024, gutter=32, mask_resolution=1024, secondary_sand=False, secondary_rock=False,
                   height_normals=False, height_metres=.04, anti_tile=True, macro=.18):
    if secondary_rock and not height_normals:
        raise ValueError('secondary rock requires height-normal mode')
    if not math.isfinite(height_metres) or not 0 < height_metres <= 1:
        raise ValueError('height relief must be0..1 metres')
    if not math.isfinite(metres) or not 0.25 <= metres <= 64:
        raise ValueError('detail metres 0.25..64')
    if not math.isfinite(transparency) or not 0 <= transparency <= .2:
        raise ValueError('underlay transparency 0..0.2')
    if not math.isfinite(macro) or not 0 <= macro <= .5:
        raise ValueError('macro variation 0..0.5')
    if not 256 <= mask_resolution <= 4096:
        raise ValueError('weight mask resolution 256..4096')
    nodes = []
    memo = {}

    def n(op, *inputs, **properties):
        signature = json.dumps([op, inputs, properties], sort_keys=True)
        if signature in memo:
            return memo[signature]
        key = 'n' + str(len(nodes))
        memo[signature] = key
        node = dict(id=key, op=op, **properties)
        if inputs:
            node['inputs'] = list(inputs)
        nodes.append(node)
        return key

    def c(value):
        return n('constant', value=value)

    def add_many(values):
        result = values[0]
        for value in values[1:]:
            result = n('add', result, value)
        return result

    uv = n('uv')
    mask_uv = n('clampRange', uv, c(.5 / mask_resolution), c(1 - .5 / mask_resolution))
    mask = n('sampleTexture', mask_uv, texture=weights_path)
    weights = [n(channel, mask) for channel in ('red', 'green', 'blue')]
    weights.append(n('clamp', n('oneMinus', add_many(weights))))
    # Quantized RGB may total slightly over one; keep energy normalized.
    total = add_many(weights)
    weights = [n('divide', weight, total) for weight in weights]
    gray = n('alpha', mask)
    grass, sand, rock, snow = weights
    weights=[grass]
    weights += [n('multiply', sand, n('oneMinus', gray)), n('multiply', sand, gray)] if secondary_sand else [sand]
    weights += [n('multiply', rock, gray), n('multiply', rock, n('oneMinus', gray))] if secondary_rock else [rock]
    weights.append(snow)
    world = n('worldPosition')
    normal = n('normal')
    # Dominant-axis projection also covers steep cliffs. Base normal is world
    # space, so detail is reconstructed in the corresponding projected basis.
    abs_normal = n('abs', normal)
    nx, ny, nz = [n(channel, abs_normal) for channel in ('red', 'green', 'blue')]
    on_x = n('greaterThan', nx, n('max', ny, nz))
    on_z = n('multiply', n('oneMinus', on_x), n('greaterThan', nz, ny))
    top_uv = n('swizzle', world, mask='rbra')
    side_x_uv = n('swizzle', world, mask='bgra')
    side_z_uv = n('swizzle', world, mask='rgra')
    projected = n('mix', n('mix', top_uv, side_x_uv, on_x), side_z_uv, on_z)
    tiled = n('multiply', projected, c(1 / metres))
    # Explicit LOD uses derivatives BEFORE fract(), avoiding atlas seam blur.
    footprint = n('max', n('abs', n('ddx', tiled)), n('abs', n('ddy', tiled)))
    footprint = n('max', n('red', footprint), n('green', footprint))
    lod = n('clampRange', n('log2', n('multiply', footprint, c(resolution))), c(0), c(4))
    wrapped = n('fract', tiled)
    # Extra lookups only for the soft ground layers (grass, then sand) and
    # only while the 16-sample material budget allows it.
    base_samples = 1 + len(weights) * (2 if height_normals else 3)
    anti_tile_layers = max(0, min(2, 16 - base_samples)) if anti_tile else 0
    if anti_tile_layers:
        # Anti-tiling: a second lookup rotated ~53 deg and rescaled, mixed by
        # world gradient noise, so the 4m repeat never lines up. Only the
        # soft ground layers (grass/sand) pay the extra sample: budget 15/16.
        tiled_b = n('add', n('multiply', n('rotateUV', tiled, c(0), c(.93)), c(1.37)), c([.37, .61, 0, 0]))
        wrapped_b = n('fract', tiled_b)
        mix_noise = n('gradientNoise', n('multiply', projected, c(1 / (metres * 1.7))))
        tile_mix = n('smoothstep', c(.32), c(.68), mix_noise)
    cell = resolution + 2 * gutter
    columns = 3 if len(weights)>4 else 2
    atlas_size = [columns * cell, 2 * cell]
    colors, normals, roughness = [], [], []
    for i, weight in enumerate(weights):
        offset = [(i % columns * cell + gutter) / atlas_size[0],
                  (i // columns * cell + gutter) / atlas_size[1], 0, 0]
        coordinate = n('add', n('multiply', wrapped, c([resolution / atlas_size[0], resolution / atlas_size[1], 0, 0])), c(offset))
        color = n('sampleTextureLod', coordinate, lod, texture=atlas_paths['basecolor'], srgb=True)
        if anti_tile and i < anti_tile_layers:
            coordinate_b = n('add', n('multiply', wrapped_b, c([resolution / atlas_size[0], resolution / atlas_size[1], 0, 0])), c(offset))
            color_b = n('sampleTextureLod', coordinate_b, lod, texture=atlas_paths['basecolor'], srgb=True)
            color = n('mix', color, color_b, tile_mix)
        detail_normal = None if height_normals else n('sampleTextureLod', coordinate, lod, texture=atlas_paths['normal'])
        orm = n('sampleTextureLod', coordinate, lod, texture=atlas_paths['orm'])
        colors.append(n('multiply', color, weight))
        if not height_normals:
            normals.append(n('multiply', detail_normal, weight))
        roughness.append(n('multiply', orm, weight))
    color = add_many(colors)
    if macro > 0:
        # Two-octave world brightness variation (23m and 7m), no texture cost.
        broad = n('gradientNoise', n('multiply', projected, c(1 / 23.0)))
        fine = n('gradientNoise', n('multiply', projected, c(1 / 7.3)))
        variation = n('add', n('multiply', broad, c(.65)), n('multiply', fine, c(.35)))
        shade = n('add', c(1 - macro), n('multiply', variation, c(2 * macro)))
        color = n('multiply', color, n('setAlpha', shade, c(1)))
    height = n('multiply', n('alpha', color), c(height_metres)) if height_normals else None
    if noise_underlay:
        # Grayscale texture is packed in mask alpha, independent of PBR alpha.
        gray = n('alpha', mask)
        underlay = n('mix', c([.06,.06,.06,1]) if secondary_sand or height_normals else c(.06),
                     c([.30,.30,.30,1]) if secondary_sand or height_normals else c(.30), gray)
        color = n('mix', color, underlay, c(transparency))
    # Normalized layer weights and opaque underlay already preserve alpha1.
    if not secondary_sand or height_normals:
        color = n('setAlpha', color, c(1))
    detail = None if height_normals else n('normalUnpack', add_many(normals))
    # Projected UV basis vectors; remove the component along the geometric N.
    if height_normals:
        final_normal=n('normalFromHeight', world, normal, height)
    elif secondary_sand:
        # Surface gradient for world-axis projection: project the encoded
        # gradient onto the shading tangent plane. Normalizing each projected
        # axis separately incorrectly boosts detail on tilted banks.
        perturb = n('mix', n('mix', n('swizzle', detail, mask='raga'),
                    n('swizzle', detail, mask='agra'), on_x),
                    n('swizzle', detail, mask='rgaa'), on_z)
        perturb = n('subtract', perturb, n('multiply', normal, n('dot3', normal, perturb)))
        final_normal = n('normalize3', n('add', n('multiply', normal, n('blue', detail)), perturb))
    else:
        tangent = n('mix', c([1, 0, 0, 0]), c([0, 0, 1, 0]), on_x)
        bitangent = n('mix', c([0, 0, 1, 0]), c([0, 1, 0, 0]), n('max', on_x, on_z))
        tangent = n('normalize3', n('subtract', tangent, n('multiply', normal, n('dot3', tangent, normal))))
        bitangent = n('normalize3', n('subtract', bitangent, n('multiply', normal, n('dot3', bitangent, normal))))
        perturb = n('add', n('multiply', tangent, n('red', detail)), n('multiply', bitangent, n('green', detail)))
        final_normal = n('normalize3', n('add', n('multiply', normal, n('blue', detail)), perturb))
    surface_orm = add_many(roughness)
    roughness_output = n('green', surface_orm)
    occlusion_output = n('red', surface_orm) if secondary_sand or height_normals else None
    metallic_output = c(0)
    if len(nodes) > 192:
        raise ValueError('terrain graph exceeds native node budget')
    outputs=dict(color=color, normal=final_normal, roughness=roughness_output, metallic=metallic_output)
    if secondary_sand or height_normals:
        outputs['occlusion']=occlusion_output
    return dict(version=1, domain='material', minQuality='low', sampleBudget=16,
                nodeBudget=max(128, len(nodes)),
                nodes=nodes, outputs=outputs)
