"""Bake seam-free object-space bark projection into an ordinary glTF PBR atlas.

Authoring only: xatlas unwraps the welded surface; projection blends texture
samples, never unrelated UV coordinates. Runtime remains standard PBR.
"""
from pathlib import Path
import json
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates, distance_transform_edt
import xatlas


def bake_bark_atlas(part, output, name, resolution=2048, material="oak_bark"):
    return bake_bark_atlas_parts([part], output, name, resolution, material)[0]


def bake_bark_atlas_parts(parts, output, name, resolution=2048, material="oak_bark"):
    """Pack rigid LOD surfaces into one shared PBR image set, without remeshing."""
    if not parts or resolution < 256 or resolution > 4096:
        raise ValueError("bounded bark parts/resolution required")
    atlas = xatlas.Atlas()
    sources = []
    for positions, normals, _, faces in parts:
        points, first, inverse = np.unique(positions, axis=0, return_index=True,
                                          return_inverse=True)
        n = normals[first]
        triangles = inverse[faces]
        atlas.add_mesh(points.astype(np.float32), triangles.astype(np.uint32))
        sources.append((points, n))
    pack = xatlas.PackOptions()
    pack.resolution = resolution
    pack.padding = 8
    atlas.generate(pack_options=pack)
    result, positions, normals, coordinates, triangles = [], [], [], [], []
    offset = 0
    for i, (points, source_normals) in enumerate(sources):
        mapping, indices, uv = atlas[i]
        p, n = points[mapping], source_normals[mapping]
        result.append((p, n, uv, indices))
        positions.append(p); normals.append(n); coordinates.append(uv)
        triangles.append(indices + offset)
        offset += len(p)
    p, n, uv = np.vstack(positions), np.vstack(normals), np.vstack(coordinates)
    indices = np.vstack(triangles)
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pkg'))
    from aiverse_pkg import package_dir
    source = package_dir('vegetation-pbr') / 'textures'  # AIVERSE-PACKAGES catalog
    catalog=json.loads((source.parent/'catalog.json').read_text())
    entry=next(m for m in catalog['materials'] if m['id']==material and m['layout']=='repeat')
    repeat_m=entry['repeatMeters']; relief_m=entry['heightMeters']*.35
    textures = {}
    for key in ('basecolor', 'height', 'roughness', 'ao'):
        a = np.asarray(Image.open(source / (material + '_' + key + '.png')))
        textures[key] = a.astype(float) / (65535 if key == 'height' else 255)

    def sample(key, pos, normal):
        a = textures[key]
        weights = np.abs(normal) ** 4
        weights /= np.maximum(weights.sum(axis=1, keepdims=True), 1e-12)
        result = np.zeros((len(pos), 3)) if a.ndim == 3 else np.zeros(len(pos))
        # Vertical grain on both side projections. Top uses X/Z.
        for axis, (u, v) in enumerate(((2, 1), (0, 2), (0, 1))):
            coords = np.array([np.mod(-pos[:, v] / repeat_m, 1) * (a.shape[0]-1),
                               np.mod(pos[:, u] / repeat_m, 1) * (a.shape[1]-1)])
            if a.ndim == 3:
                value = np.stack([map_coordinates(a[:, :, c], coords, order=1,
                                                 mode='wrap') for c in range(3)], axis=1)
                result += value * weights[:, axis, None]
            else:
                result += map_coordinates(a, coords, order=1, mode='wrap') * weights[:, axis]
        return result

    color = np.zeros((resolution, resolution, 3), np.float32)
    normal_map = np.zeros_like(color)
    orm = np.zeros_like(color)
    covered = np.zeros((resolution, resolution), bool)
    for face in indices:
        tuv = uv[face] * (resolution-1)
        lo = np.maximum(np.floor(tuv.min(axis=0)).astype(int), 0)
        hi = np.minimum(np.ceil(tuv.max(axis=0)).astype(int), resolution-1)
        x, y = np.meshgrid(np.arange(lo[0], hi[0]+1), np.arange(lo[1], hi[1]+1))
        e1, e2 = tuv[1]-tuv[0], tuv[2]-tuv[0]
        det = e1[0]*e2[1]-e1[1]*e2[0]
        if abs(det) < 1e-10:
            continue
        dx, dy = x-tuv[0, 0], y-tuv[0, 1]
        b = (dx*e2[1]-dy*e2[0])/det
        c = (dy*e1[0]-dx*e1[1])/det
        inside = (b >= -1e-5) & (c >= -1e-5) & (b+c <= 1.00001)
        if not inside.any():
            continue
        x, y, b, c = x[inside], y[inside], b[inside], c[inside]
        bary = np.c_[1-b-c, b, c]
        pos = bary @ p[face]
        nor = bary @ n[face]
        nor /= np.maximum(np.linalg.norm(nor, axis=1, keepdims=True), 1e-12)
        # Atlas tangent frame; the baked normal remains ordinary tangent-space.
        dp1, dp2 = p[face[1]]-p[face[0]], p[face[2]]-p[face[0]]
        tangent = (dp1*e2[1]-dp2*e1[1])/det
        tangent = tangent[None, :] - nor*(nor @ tangent)[:, None]
        tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-12)
        bitangent = np.cross(nor, tangent)
        dpdv = (dp2*e1[0]-dp1*e2[0])/det
        bitangent *= np.where(bitangent @ dpdv < 0, -1, 1)[:, None]
        epsilon = .002
        gx = (sample('height', pos+tangent*epsilon, nor)-sample('height', pos-tangent*epsilon, nor))*relief_m/(2*epsilon)
        gy = (sample('height', pos+bitangent*epsilon, nor)-sample('height', pos-bitangent*epsilon, nor))*relief_m/(2*epsilon)
        nt = np.c_[-gx, -gy, np.ones(len(gx))]
        nt /= np.linalg.norm(nt, axis=1, keepdims=True)
        color[y, x] = sample('basecolor', pos, nor)
        normal_map[y, x] = nt*.5+.5
        orm[y, x, 0] = sample('ao', pos, nor)
        orm[y, x, 1] = sample('roughness', pos, nor)
        covered[y, x] = True
    if not covered.any():
        raise ValueError('Empty bark atlas')
    nearest = distance_transform_edt(~covered, return_distances=False, return_indices=True)
    for suffix, data in [('basecolor', color), ('normal', normal_map), ('orm', orm)]:
        data[~covered] = data[nearest[0][~covered], nearest[1][~covered]]
        Image.fromarray(np.rint(np.clip(data, 0, 1)*255).astype('uint8')).save(output / (name+'_bark_'+suffix+'.png'))
    return result
