"""Derived textures for packed Unity maps (channel repacking with bytes slicing: no per-pixel Python loops).

ops (spec from urp.Mapper.derive):
  occlusion            R=G=B = source.G (URP samples occlusion from G, the engine from R)
  metallic_gloss       glTF metallic-roughness: G = 1 - smoothness * A, B = metallic R;
                       params smoothness, albedoAlpha (then A comes from the second source)
  roughness_from_alpha G = 1 - smoothness * A of the source, B = 1 (metallic factor stays the material's)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'unity_vfx'))
import images  # noqa: E402  (shared decoder: PNG / TGA, PSD/TIFF via an external converter)


def _resize_nearest(img, w, h):
    if (img.w, img.h) == (w, h):
        return img
    out = bytearray(w * h * 4)
    xs = [min(img.w - 1, x * img.w // w) * 4 for x in range(w)]
    for y in range(h):
        sy = min(img.h - 1, y * img.h // h) * img.w * 4
        row = img.rgba
        o = y * w * 4
        for x, sx in enumerate(xs):
            out[o + x * 4:o + x * 4 + 4] = row[sy + sx:sy + sx + 4]
    return images.Image(w, h, out)


def _table(fn):
    return bytes(max(0, min(255, int(round(fn(v))))) for v in range(256))


def apply(op, srcs, params):
    """srcs: list of images.Image -> images.Image."""
    a = srcs[0]
    n = a.w * a.h
    px = bytes(a.rgba)
    out = bytearray(n * 4)
    full = b'\xff' * n
    if op == 'occlusion':
        g = px[1::4]
        out[0::4] = g
        out[1::4] = g
        out[2::4] = g
        out[3::4] = full
    elif op in ('metallic_gloss', 'roughness_from_alpha'):
        s = float(params.get('smoothness', 1))
        rough = _table(lambda v: 255 - v * s)
        alpha_src = px
        if op == 'metallic_gloss' and params.get('albedoAlpha') and len(srcs) > 1:
            alpha_src = bytes(_resize_nearest(srcs[1], a.w, a.h).rgba)
        out[0::4] = full
        out[1::4] = alpha_src[3::4].translate(rough)
        out[2::4] = px[0::4] if op == 'metallic_gloss' else full
        out[3::4] = full
    else:
        raise ValueError('unknown texture op ' + op)
    return images.Image(a.w, a.h, out)


def run(spec, root, write):
    """Decode the sources under `root`, apply, write(out_relative, png_bytes). -> (ok, note)."""
    imgs = []
    for rel in spec['sources']:
        img, note = images.load_any(str(Path(root) / rel))
        if img is None:
            return False, '%s: %s' % (rel, note)
        imgs.append(img)
    result = apply(spec['op'], imgs, spec.get('params') or {})
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / 'out.png'
        images.write_png(str(p), result)
        write(spec['out'], p.read_bytes())
    return True, None
