"""Unity material (.mat) -> AIVERSE material override (docs/models.md § Materials).

Covers URP Lit / SimpleLit / Unlit / BakedLit / Particles, built-in Standard and legacy shaders,
Shader Graph materials (through shadergraph.py) and a property-name fallback for hand-written
shaders, which are reported as manual work. Packed maps become derived textures (texops.py):
Unity keeps metallic in R and smoothness in A of _MetallicGlossMap and occlusion in G, the engine
reads glTF metallic-roughness (G = roughness, B = metallic) and occlusion in R.
"""
import hashlib
import re
from pathlib import Path

from lib.unity.unity_yaml import get

BUILTIN_GUID = '0000000000000000f000000000000000'
# GUIDs of URP package shaders (com.unity.render-pipelines.universal, stable across versions)
URP = {
    '933532a4fcc9baf4fa0491de14d08ed7': ('urp-lit', 'Universal Render Pipeline/Lit'),
    '8d2bb70cbf9db8d4da26e15b26e74248': ('urp-simplelit', 'Universal Render Pipeline/Simple Lit'),
    '650dd9526735d5b46b79224bc6e94025': ('urp-unlit', 'Universal Render Pipeline/Unlit'),
    '0ca6dca7396eb48e5849247ffd444914': ('urp-bakedlit', 'Universal Render Pipeline/Baked Lit'),
    '0406db5a14f94604a8c57ccfbc9f3b46': ('urp-particles-unlit', 'Universal Render Pipeline/Particles/Unlit'),
    'b7839dad95683814aa64166edc107ae2': ('urp-particles-lit', 'Universal Render Pipeline/Particles/Lit'),
    '8516d7a69675844a7a0b7095af7c46af': ('urp-particles-simplelit', 'Universal Render Pipeline/Particles/Simple Lit'),
    '69c1f799e772cb6438f56c23efccb782': ('urp-terrain', 'Universal Render Pipeline/Terrain/Lit'),
    '9b4e681081e2b4c469111bb649e2f7ee': ('urp-decal', 'Shader Graphs/Decal'),
}
# Built-in shaders by file id (guid 0000000000000000f000000000000000)
BUILTIN = {
    46: ('builtin-standard', 'Standard'), 45: ('builtin-standard', 'Standard (Specular setup)'),
    7: ('builtin-legacy', 'Legacy Shaders/Diffuse'), 2: ('builtin-legacy', 'Legacy Shaders/Bumped Diffuse'),
    10703: ('builtin-legacy', 'Mobile/Diffuse'), 10752: ('builtin-legacy', 'Unlit/Texture'),
    10755: ('builtin-legacy', 'Unlit/Color'), 10750: ('builtin-legacy', 'Unlit/Transparent'),
    10751: ('builtin-legacy', 'Unlit/Transparent Cutout'), 10762: ('builtin-ui', 'UI/Default'),
    10720: ('builtin-particles', 'Mobile/Particles/Additive'), 10721: ('builtin-particles', 'Mobile/Particles/Alpha Blended'),
    200: ('builtin-particles', 'Legacy Shaders/Particles/Additive'), 202: ('builtin-particles', 'Legacy Shaders/Particles/Alpha Blended'),
    203: ('builtin-particles', 'Legacy Shaders/Particles/Alpha Blended Premultiply'),
    208: ('builtin-particles', 'Legacy Shaders/Particles/~Additive-Multiply'),
    211: ('builtin-particles', 'Particles/Standard Unlit'), 103: ('builtin-skybox', 'Skybox/6 Sided'),
    104: ('builtin-skybox', 'Skybox/Cubemap'), 106: ('builtin-skybox', 'Skybox/Procedural'), 108: ('builtin-skybox', 'Skybox/Panoramic'),
}
IMAGE_EXT = ('.png', '.tga', '.jpg', '.jpeg', '.psd', '.tif', '.tiff', '.bmp')


def srgb_to_linear(c):
    c = float(c)
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _round(x, d=4):
    return round(float(x), d)


class Props:
    """m_SavedProperties of a material plus its keywords (old m_ShaderKeywords and new m_ValidKeywords)."""

    def __init__(self, data):
        saved = data.get('m_SavedProperties') or {}

        def pairs(key):
            out = {}
            for e in saved.get(key) or []:
                if isinstance(e, dict):
                    out.update(e)
            return out
        self.tex, self.floats, self.colors = pairs('m_TexEnvs'), pairs('m_Floats'), pairs('m_Colors')
        kw = data.get('m_ValidKeywords')
        words = list(kw) if isinstance(kw, list) else []
        words += str(data.get('m_ShaderKeywords') or '').split()
        self.keywords = set(w for w in words if isinstance(w, str))
        q = data.get('m_CustomRenderQueue', -1)
        self.queue = q if isinstance(q, int) else -1
        self.tags = data.get('stringTagMap') or {}
        if not isinstance(self.tags, dict):
            self.tags = {}

    def f(self, name, default=None):
        v = self.floats.get(name, default)
        return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else default

    def color(self, name, default=None):
        c = self.colors.get(name)
        if not isinstance(c, dict):
            return default
        return [float(c.get(k, d)) for k, d in zip('rgba', (1, 1, 1, 1))]

    def texref(self, name):
        t = self.tex.get(name)
        if not isinstance(t, dict):
            return None
        ref = t.get('m_Texture') or {}
        return ref if isinstance(ref, dict) and get(ref, 'fileID') else None

    def st(self, name):
        t = self.tex.get(name) or {}
        s, o = t.get('m_Scale') or {}, t.get('m_Offset') or {}
        return [float(s.get('x', 1)), float(s.get('y', 1))], [float(o.get('x', 0)), float(o.get('y', 0))]


class Context:
    """Project access for material conversion. `assets` is lib.unity.unity_scene.Assets."""

    def __init__(self, root, assets, linear=True, graphs=None):
        self.root, self.assets, self.linear = Path(root), assets, linear
        self.graphs = graphs   # shadergraph.Library or None (graphs then become fallbacks)

    def rel(self, ref):
        p = self.assets.path(ref)
        if not p:
            return ''
        try:
            return Path(p).resolve().relative_to(self.root.resolve()).as_posix()
        except ValueError:
            return ''

    def meta(self, rel):
        return self.assets.meta(str(self.root / rel)) if rel else {}


def identify(data, ctx):
    """(family, shader name, shader asset path relative to the project or '')."""
    ref = data.get('m_Shader') or {}
    guid, fid = str(ref.get('guid', '')).lower(), ref.get('fileID')
    if guid in URP:
        return URP[guid] + ('',)
    if guid == BUILTIN_GUID:
        fam, name = BUILTIN.get(fid, ('builtin-other', 'built-in shader %s' % fid))
        return fam, name, ''
    rel = ctx.rel(ref)
    if rel.endswith('.shadergraph'):
        return 'shadergraph', Path(rel).stem, rel
    if rel:
        name = Path(rel).stem
        try:
            m = re.search(r'Shader\s+"([^"]+)"', (ctx.root / rel).read_text(errors='replace')[:4000])
            name = m[1] if m else name
        except OSError:
            pass
        return 'custom', name, rel
    return 'unknown', 'shader %s:%s' % (guid or '-', fid), ''


class Result:
    def __init__(self, name, family, shader, shader_path):
        self.name, self.family, self.shader, self.shader_path = name, family, shader, shader_path
        self.material = {'replace': True}
        self.derived = []      # texops specs
        self.textures = []     # (relative source path, role) for importer settings
        self.items = []        # (status exact|approx|missing, reason)
        self.graph = None      # graph conversion report when the shader is a Shader Graph

    def note(self, status, reason):
        if (status, reason) not in self.items:
            self.items.append((status, reason))

    def status(self):
        s = [i[0] for i in self.items]
        return 'missing' if 'missing' in s else 'approx' if 'approx' in s else 'exact'

    def as_json(self):
        return dict(name=self.name, family=self.family, shader=self.shader, shaderPath=self.shader_path,
                    material=self.material, derived=self.derived, textures=[list(t) for t in self.textures],
                    items=[list(i) for i in self.items], status=self.status(), graph=self.graph)


def content(rel):
    return 'content/' + rel


def derived_path(op, sources, params):
    key = hashlib.sha256(repr((op, sources, sorted(params.items()))).encode()).hexdigest()[:10]
    stem = re.sub(r'[^A-Za-z0-9_]+', '_', Path(sources[0]).stem)[:40]
    return 'content/_derived/%s_%s_%s.png' % (stem, op, key)


class Mapper:
    def __init__(self, ctx, props, res):
        self.ctx, self.p, self.r = ctx, props, res

    def lin(self, c):
        if not c:
            return c
        rgb = [srgb_to_linear(x) if self.ctx.linear else x for x in c[:3]]
        return [_round(x) for x in rgb] + [_round(x) for x in c[3:]]

    def texture(self, prop, role):
        ref = self.p.texref(prop)
        if not ref:
            return None
        rel = self.ctx.rel(ref)
        if not rel:
            self.r.note('missing', '%s: texture %s is built-in or outside the project' % (prop, ref.get('guid', '?')))
            return None
        if Path(rel).suffix.lower() not in IMAGE_EXT:
            self.r.note('missing', '%s: %s is not a 2D image the engine reads' % (prop, rel))
            return None
        if (rel, role) not in self.r.textures:
            self.r.textures.append((rel, role))
        return rel

    def derive(self, op, sources, params):
        out = derived_path(op, sources, params)
        spec = dict(op=op, sources=sources, params=params, out=out)
        if spec not in self.r.derived:
            self.r.derived.append(spec)
        return out

    # ------------------------------------------------------------------ shared pieces
    def tiling(self, prop):
        if prop not in self.p.tex:
            return
        (sx, sy), (ox, oy) = self.p.st(prop)
        if [sx, sy] != [1, 1]:
            self.r.material['uvScale'] = [_round(sx), _round(sy)]
        # engine UVs have V flipped against Unity (FBX/OBJ loaders): offset.y' = 1 - scale.y - offset.y
        ey = 1 - sy - oy
        if [ox, ey] != [0, 0]:
            self.r.material['uvOffset'] = [_round(ox), _round(ey)]

    def surface(self, transparent_key='_Surface', blend_key='_Blend', clip_key='_AlphaClip', cutoff_key='_Cutoff'):
        m, p = self.r.material, self.p
        transparent = p.f(transparent_key, 0) == 1 or '_SURFACE_TYPE_TRANSPARENT' in p.keywords
        clip = p.f(clip_key, 0) == 1 or '_ALPHATEST_ON' in p.keywords
        if clip:
            m['alpha'] = 'mask'
            m['cutoff'] = _round(p.f(cutoff_key, 0.5))
        if transparent:
            blend = int(p.f(blend_key, 0) or 0)
            m['alpha'] = 'blend'
            m['castShadow'] = False
            if clip:
                self.r.note('approx', 'Alpha clip and transparent surface together: drawn as blend without the clip threshold.')
            if blend == 1:
                self.r.note('approx', 'Premultiplied blending drawn as straight alpha.')
            elif blend in (2, 3):
                self.r.note('approx', '%s blending has no model material equivalent (straight alpha only); use a particle effect or a Shader Graph.'
                            % ('Additive' if blend == 2 else 'Multiply'))
        cull = p.f('_Cull', 2)
        if cull == 0:
            m['doubleSided'] = True
        elif cull == 1:
            self.r.note('approx', 'Front-face culling (back faces only) is not supported; drawn single-sided.')
        if p.f('_ReceiveShadows', 1) == 0 or '_RECEIVE_SHADOWS_OFF' in p.keywords:
            m['receiveShadow'] = False

    def base(self, color_props=('_BaseColor', '_Color'), tex_props=('_BaseMap', '_MainTex')):
        for c in color_props:
            col = self.p.color(c)
            if col:
                self.r.material['color'] = self.lin(col)
                break
        for t in tex_props:
            rel = self.texture(t, 'base')
            if rel:
                self.r.material['texture'] = content(rel)
                self.tiling(t)
                return rel
        return None

    def normal(self, prop='_BumpMap', scale='_BumpScale'):
        rel = self.texture(prop, 'normal')
        if rel:
            self.r.material['normalMap'] = content(rel)
            s = self.p.f(scale, 1)
            if s != 1:
                self.r.material['normalScale'] = _round(s)

    def emission(self, require_keyword=True):
        p, m = self.p, self.r.material
        on = '_EMISSION' in p.keywords or not require_keyword
        col = p.color('_EmissionColor')
        rel = self.texture('_EmissionMap', 'emission') if on else None
        if on and (rel or (col and any(x > 0 for x in col[:3]))):
            c = self.lin(col or [1, 1, 1, 1])[:3]
            peak = max(c) if c else 0
            if peak > 1:   # HDR colour: keep the hue in emissive, the intensity in emissiveStrength
                m['emissive'] = [_round(x / peak) for x in c]
                m['emissiveStrength'] = _round(peak)
            else:
                m['emissive'] = c
            if rel:
                m['emissiveMap'] = content(rel)

    def occlusion(self, prop='_OcclusionMap', strength='_OcclusionStrength'):
        rel = self.texture(prop, 'data')
        if rel:
            # Unity samples occlusion from G, the engine from R
            self.r.material['occlusionMap'] = self.derive('occlusion', [rel], {})
            s = self.p.f(strength, 1)
            if s != 1:
                self.r.material['occlusionStrength'] = _round(s)

    def detail(self):
        p = self.p
        wanted = '_DETAIL_MULX2' in p.keywords or '_DETAIL_SCALED' in p.keywords
        rel = self.texture('_DetailAlbedoMap', 'detail') if wanted else None
        if p.texref('_DetailNormalMap') and wanted:
            self.r.note('approx', 'Detail normal map is not blended (no detail normal in the engine).')
        if not rel:
            return
        mask = self.texture('_DetailMask', 'data')
        base_s, base_o = p.st('_BaseMap' if '_BaseMap' in p.tex else '_MainTex')
        ds, do = p.st('_DetailAlbedoMap')
        from . import shadergraph
        self.r.material['shaderGraph'] = shadergraph.detail_graph(content(rel), content(mask) if mask else None,
                                                                  base_s, base_o, ds, do, p.f('_DetailAlbedoMapScale', 1))
        self.r.note('approx', 'Detail albedo (x2 multiply, mask alpha) generated as a material Shader Graph.')
        if p.f('_UVSec', 0) == 1:
            self.r.note('approx', 'Detail map uses UV1; the engine samples UV0.')


def _lit(mp):
    p, m, r = mp.p, mp.r.material, mp.r
    m['vertexColors'] = False
    base = mp.base()
    mp.surface()
    mp.normal()
    mp.emission()
    mp.occlusion()
    mp.detail()
    smooth = p.f('_Smoothness', p.f('_Glossiness', 0.5))
    albedo_alpha = (p.f('_SmoothnessTextureChannel', 0) == 1 or '_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A' in p.keywords) and bool(base)
    if p.f('_WorkflowMode', 1) == 0 or '_SPECULAR_SETUP' in p.keywords:
        m['metallic'] = 0
        spec = mp.texture('_SpecGlossMap', 'data')
        src = base if albedo_alpha else spec
        if src:
            m['metallicRoughnessMap'] = mp.derive('roughness_from_alpha', [src], dict(smoothness=_round(smooth)))
            m['roughness'] = 1
        else:
            m['roughness'] = _round(1 - smooth)
        r.note('approx', 'Specular workflow: specular colour dropped, metallic 0, roughness from smoothness.')
    else:
        metal = mp.texture('_MetallicGlossMap', 'data')
        if metal:
            # URP: metallic = map.r (factor ignored), smoothness = (albedo or map).a * _Smoothness
            m['metallicRoughnessMap'] = mp.derive('metallic_gloss', [metal] + ([base] if albedo_alpha else []),
                                                  dict(smoothness=_round(smooth), albedoAlpha=albedo_alpha))
            m['metallic'] = 1
            m['roughness'] = 1
        elif albedo_alpha:
            m['metallicRoughnessMap'] = mp.derive('roughness_from_alpha', [base], dict(smoothness=_round(smooth)))
            m['metallic'] = _round(p.f('_Metallic', 0))
            m['roughness'] = 1
        else:
            m['metallic'] = _round(p.f('_Metallic', 0))
            m['roughness'] = _round(1 - smooth)
    if p.f('_SpecularHighlights', 1) == 0 or '_SPECULARHIGHLIGHTS_OFF' in p.keywords:
        r.note('approx', 'Specular highlights off has no engine switch.')
    if p.f('_EnvironmentReflections', 1) == 0 or '_ENVIRONMENTREFLECTIONS_OFF' in p.keywords:
        r.note('approx', 'Environment reflections off has no engine switch.')
    if p.texref('_ParallaxMap') and '_PARALLAXMAP' in p.keywords:
        r.note('approx', 'Height (parallax) map dropped.')
    if p.f('_ClearCoat', 0) == 1 or '_CLEARCOAT' in p.keywords:
        r.note('approx', 'Clear coat dropped.')


def _simplelit(mp):
    p, m, r = mp.p, mp.r.material, mp.r
    m['vertexColors'] = False
    base = mp.base()
    mp.surface()
    mp.normal()
    mp.emission()
    m['metallic'] = 0
    smooth = p.f('_Smoothness', p.f('_Shininess', 0.5))
    from_base = (p.f('_SmoothnessSource', 0) == 1 or '_GLOSSINESS_FROM_BASE_ALPHA' in p.keywords) and bool(base)
    spec = mp.texture('_SpecGlossMap', 'data') if '_SPECGLOSSMAP' in p.keywords else None
    src = base if from_base else spec
    if src:
        m['metallicRoughnessMap'] = mp.derive('roughness_from_alpha', [src], dict(smoothness=_round(smooth)))
        m['roughness'] = 1
    else:
        m['roughness'] = _round(1 - smooth)
    r.note('approx', 'Simple Lit (Blinn-Phong) mapped to PBR: specular colour dropped, roughness from smoothness.')


def _unlit(mp, vertex_colors=False):
    m = mp.r.material
    m['unlit'] = True
    m['vertexColors'] = vertex_colors
    mp.base()
    mp.surface()


def _particles(mp, lit):
    m = mp.r.material
    if lit:
        _lit(mp)
        m['vertexColors'] = True
    else:
        _unlit(mp, vertex_colors=True)
        mp.emission()
    if '_FADING_ON' in mp.p.keywords or '_SOFTPARTICLES_ON' in mp.p.keywords:
        mp.r.note('approx', 'Soft particles / camera fading need scene depth; dropped.')
    if '_FLIPBOOKBLENDING_ON' in mp.p.keywords:
        mp.r.note('approx', 'Flipbook frame blending dropped.')
    mp.r.note('approx', 'Particle shader on a mesh: unlit vertex-coloured material; particle systems go through the VFX importer.')


def _standard(mp):
    p, m = mp.p, mp.r.material
    m['vertexColors'] = False
    base = mp.base(('_Color',), ('_MainTex',))
    mode = int(p.f('_Mode', 0) or 0)
    if mode == 1:
        m['alpha'] = 'mask'
        m['cutoff'] = _round(p.f('_Cutoff', 0.5))
    elif mode in (2, 3):
        m['alpha'] = 'blend'
        m['castShadow'] = False
        if mode == 3:
            mp.r.note('approx', 'Standard Transparent (premultiplied reflections) drawn as Fade.')
    mp.normal()
    mp.emission()
    mp.occlusion()
    mp.detail()
    metal = mp.texture('_MetallicGlossMap', 'data')
    smooth = p.f('_GlossMapScale', 1) if metal else p.f('_Glossiness', 0.5)
    albedo_alpha = p.f('_SmoothnessTextureChannel', 0) == 1 and bool(base)
    if metal:
        m['metallicRoughnessMap'] = mp.derive('metallic_gloss', [metal] + ([base] if albedo_alpha else []),
                                              dict(smoothness=_round(smooth), albedoAlpha=albedo_alpha))
        m['metallic'] = 1
        m['roughness'] = 1
    else:
        m['metallic'] = _round(p.f('_Metallic', 0))
        m['roughness'] = _round(1 - smooth)
    if mp.r.shader.startswith('Standard (Specular'):
        mp.r.note('approx', 'Standard specular setup: specular colour dropped.')


def fallback(mp, status, why):
    """Common property names of hand-written / unknown shaders, reported for manual work."""
    m = mp.r.material
    mp.base(('_BaseColor', '_Color', '_TintColor', '_MainColor'), ('_BaseMap', '_MainTex', '_Albedo', '_AlbedoTex', '_BaseTex'))
    if mp.p.texref('_BumpMap'):
        mp.normal('_BumpMap')
    elif mp.p.texref('_NormalMap'):
        mp.normal('_NormalMap', '_NormalStrength')
    if mp.p.texref('_EmissionMap'):
        mp.emission(require_keyword=False)
    if mp.p.queue >= 2450 or str(mp.p.tags.get('RenderType', '')).lower() == 'transparent':
        m['alpha'] = 'blend' if mp.p.queue >= 2500 or mp.p.queue == -1 else 'mask'
    mp.r.note(status, why)


def convert(data, ctx, name=None, domain='material'):
    """-> Result for one material document (dict from unity_yaml)."""
    family, shader, path = identify(data, ctx)
    res = Result(name or str(data.get('m_Name', '?')), family, shader, path)
    mp = Mapper(ctx, Props(data), res)
    if family in ('urp-lit', 'urp-terrain'):
        _lit(mp)
        if family == 'urp-terrain':
            res.note('missing', 'Terrain Lit material: splat layers come from TerrainData (not converted).')
    elif family == 'urp-simplelit':
        _simplelit(mp)
    elif family in ('urp-unlit', 'urp-bakedlit'):
        _unlit(mp)
        if family == 'urp-bakedlit':
            mp.normal()
            res.note('approx', 'Baked Lit drawn unlit (lightmaps are not imported).')
    elif family in ('urp-particles-unlit', 'urp-particles-lit', 'urp-particles-simplelit'):
        _particles(mp, family != 'urp-particles-unlit')
    elif family == 'builtin-standard':
        _standard(mp)
    elif family in ('builtin-legacy', 'builtin-particles', 'builtin-ui'):
        if family == 'builtin-legacy' and 'Unlit' not in shader:
            _standard(mp)
        else:
            _unlit(mp, vertex_colors=family != 'builtin-legacy')
        if family == 'builtin-particles' or shader.endswith('Transparent'):
            res.material['alpha'] = 'blend'
        elif shader.endswith('Cutout'):
            res.material['alpha'] = 'mask'
            res.material['cutoff'] = _round(mp.p.f('_Cutoff', 0.5))
        res.note('approx', 'Built-in shader %s mapped by its common properties.' % shader)
    elif family == 'shadergraph':
        done = ctx.graphs.apply(path, mp, domain) if ctx.graphs else False
        if not done:
            fallback(mp, 'missing', 'Shader Graph %s not converted; base colour/texture only.' % path)
    elif family == 'urp-decal':
        fallback(mp, 'missing', 'URP decal material: projected decals are not models; base texture only.')
    elif family == 'builtin-skybox':
        res.material = {}
        res.note('missing', 'Skybox material (%s): set the game lighting sky instead.' % shader)
    elif family == 'custom':
        fallback(mp, 'missing', 'Hand-written shader "%s" (%s): manual port (Shader Graph or engine material); base colour/texture only.'
                 % (shader, path))
    else:
        fallback(mp, 'missing', 'Unknown shader (%s): base colour/texture only.' % shader)
    return res


def project_linear(root):
    """True when ProjectSettings uses the linear colour space (material colours then convert sRGB -> linear)."""
    p = Path(root) / 'ProjectSettings/ProjectSettings.asset'
    try:
        m = re.search(r'm_ActiveColorSpace:\s*(\d+)', p.read_text(errors='replace'))
    except OSError:
        return True
    return not m or m[1] == '1'
