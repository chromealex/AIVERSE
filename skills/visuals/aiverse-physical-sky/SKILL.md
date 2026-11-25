---
name: aiverse-physical-sky
description: Configure a map's ordinary daylight, sunset, clouds and atmosphere (небо, облака, закат) using native AIVERSE PRISMA Physical Sky. Use when replacing a space backdrop or choosing how an outdoor game scene renders the sky.
---

# Author a map sky

For an outdoor scene, start with PRISMA Physical Sky. It renders the atmosphere, sun disk and optional clouds in the game. A generated panorama is appropriate when the user explicitly asks for painted backdrop art. Keep another map's authored space background scoped to that map.

1. Inspect the map, its game defaults and the active rendering host. Start with [the working preset](../../../examples/render/physical-sky.json). In the selected map, set `postfx.hdr` to `"on"` and `postfx.physicalSky.enabled` to `true`. Individual map fields override game defaults. Remove or suppress this map's old opaque sky mesh so background pixels retain far depth.
2. In hosts using PRISMA configuration, load the map postfx block through the normal configure path. For a custom host, pass the game/map **postfx objects** to `render::parsePhysicalSky`, apply the result with `Prisma::setPhysicalSky`, and render and resolve the scene through that PRISMA instance. Reconfigure when switching maps. Set the scene sun direction/color/intensity on `Renderer3D::lighting` so shadows agree with the sky.
3. Use Medium or High post quality and an HDR-capable device. Start phone testing with god rays off. Measure clear atmosphere and clouds separately on the target GPU. On a device where HDR or the required quality is unavailable, verify the host's ordinary background visually.
4. Capture a horizon shot in the actual client with foreground geometry, then switch to a differently configured map and back. For reproducible cloud comparisons freeze the presentation time with `setPhysicalSkyTime`. For weather driven by simulation, use the existing PHYSIS binding.

[Runtime parameters, quality limits and GPU fixtures](../../../docs/physical-sky.md) · [PRISMA atmosphere integration](../aiverse-prisma/references/atmosphere.md)
