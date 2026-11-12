"""Unity Shader Graph (.shadergraph / .shadersubgraph, serialization v3) -> AIVERSE Shader Graph (docs/shader-graph.md).

A Unity graph is converted once into a template; every material that uses it gets a copy with its
property values (`parameters`) and texture paths filled in (`instantiate`). Coverage is the node set
Drone Fall uses (SUPPORTED below). Nodes the engine cannot express are replaced by a neutral value
and listed (UNSUPPORTED): scene depth reads as far away, so soft-particle fades become 1, custom
functions and scene colour are manual work. Unity UVs have V up, engine UVs V down: UV sources are
flipped into Unity space and flipped back at each texture sample, so UV math stays exact.
"""
import json
import math
import re
from pathlib import Path

ENGINE_OPS = {  # id: (inputs, domain bits material=1 postfx=2 particle=4, vertex-eligible) -- engine/render/graph/ShaderGraph.cpp
    'constant': (0, 7, 1), 'parameter': (0, 7, 1), 'uv': (0, 7, 1), 'input': (0, 7, 0), 'texture': (1, 7, 0),
    'sampleTexture': (1, 7, 0), 'sampleTextureLod': (2, 1, 1), 'time': (0, 7, 1), 'worldPosition': (0, 5, 1),
    'objectPosition': (0, 1, 1), 'normal': (0, 1, 1), 'viewDirection': (0, 5, 1), 'vertexColor': (0, 5, 1),
    'screenPosition': (0, 7, 0), 'add': (2, 7, 1), 'subtract': (2, 7, 1), 'multiply': (2, 7, 1), 'divide': (2, 7, 1),
    'mix': (3, 7, 1), 'sin': (1, 7, 1), 'fract': (1, 7, 1), 'clamp': (1, 7, 1), 'oneMinus': (1, 7, 1), 'dot': (2, 7, 1),
    'length': (1, 7, 1), 'step': (2, 7, 1), 'red': (1, 7, 1), 'green': (1, 7, 1), 'blue': (1, 7, 1), 'alpha': (1, 7, 1),
    'abs': (1, 7, 1), 'negate': (1, 7, 1), 'floor': (1, 7, 1), 'cos': (1, 7, 1), 'atan2': (2, 7, 1), 'pow': (2, 7, 1),
    'min': (2, 7, 1), 'max': (2, 7, 1), 'modulo': (2, 7, 1), 'smoothstep': (3, 7, 1), 'clampRange': (3, 7, 1),
    'remap': (5, 7, 1), 'dot3': (2, 7, 1), 'length3': (1, 7, 1), 'distance': (2, 7, 1), 'distance3': (2, 7, 1),
    'normalize3': (1, 7, 1), 'fresnel': (3, 7, 1), 'combine': (4, 7, 1), 'swizzle': (1, 7, 1), 'setAlpha': (2, 7, 1),
    'tilingOffset': (3, 7, 1), 'rotateUV': (3, 7, 1), 'noise': (1, 7, 1), 'gradientNoise': (1, 7, 1), 'voronoi': (1, 7, 1),
    'normalUnpack': (1, 7, 1), 'normalStrength': (2, 7, 1), 'lessThan': (2, 7, 1), 'greaterThan': (2, 7, 1),
    'equal': (2, 7, 1), 'branch': (3, 7, 1), 'fwidth': (1, 7, 0), 'radians': (1, 7, 1), 'sqrt': (1, 7, 1),
    'saturation': (2, 7, 1), 'rgb': (1, 7, 1), 'append': (2, 7, 1),
}
DOMAIN_BIT = {'material': 1, 'postfx': 2, 'particle': 4}
MAX_NODES, MAX_SAMPLES, MAX_TEXTURES = 128, 8, 4

SUPPORTED = sorted("""PropertyNode MultiplyNode AddNode SubtractNode DivideNode PowerNode OneMinusNode SaturateNode ClampNode
LerpNode SmoothstepNode StepNode RemapNode AbsoluteNode NegateNode MaximumNode MinimumNode ModuloNode SineNode CosineNode
DotProductNode SplitNode CombineNode SwizzleNode BranchNode ComparisonNode BooleanNode Vector1Node Vector2Node Vector3Node
Vector4Node ColorNode SampleTexture2DNode SampleTexture2DLODNode UVNode TilingAndOffsetNode TimeNode VertexColorNode
PositionNode NormalVectorNode ViewDirectionNode FresnelNode ScreenPositionNode PolarCoordinatesNode TwirlNode
RadialShearNode RotateNode RectangleNode SphereMaskNode NoiseNode GradientNoiseNode VoronoiNode NormalStrengthNode
NormalUnpackNode Texture2DPropertiesNode SplitTextureTransformNode RedirectNodeData PreviewNode IsFrontFaceNode
SubGraphNode FractionNode FloorNode LengthNode NormalizeNode DistanceNode SquareRootNode ReciprocalNode""".split())
# replaced by a neutral value, with the reason
UNSUPPORTED = {
    'SceneDepthNode': (100000.0, 'scene depth is not available to graphs: reads as far away (soft-particle / intersection fades become 1)'),
    'SceneColorNode': (0.0, 'scene colour (opaque texture) is not available to material graphs'),
    'CameraNode': (0.0, 'camera position/direction outputs are not graph inputs'),
    'CustomFunctionNode': (1.0, 'custom HLSL function: manual port'),
    'SampleCubemapNode': (0.5, 'cube maps are not sampled by graphs'),
    'ParallaxOcclusionMappingNode': (None, 'parallax occlusion mapping: UVs pass through unchanged'),
    'KeywordNode': (None, 'shader keywords: the first (default) branch is used'),
}
PROPERTY_WIDTH = {'Vector1ShaderProperty': 1, 'Vector2ShaderProperty': 2, 'Vector3ShaderProperty': 3, 'Vector4ShaderProperty': 4,
                  'ColorShaderProperty': 4, 'BooleanShaderProperty': 1}
SLOT_WIDTH = {'Vector1MaterialSlot': 1, 'Vector2MaterialSlot': 2, 'Vector3MaterialSlot': 3, 'Vector4MaterialSlot': 4,
              'ColorRGBAMaterialSlot': 4, 'ColorRGBMaterialSlot': 3, 'UVMaterialSlot': 2, 'PositionMaterialSlot': 3,
              'NormalMaterialSlot': 3, 'TangentMaterialSlot': 3, 'ViewDirectionMaterialSlot': 3, 'ScreenPositionMaterialSlot': 4,
              'BooleanMaterialSlot': 1, 'VertexColorMaterialSlot': 4}
DEFAULT_TEXTURE = {0: [1, 1, 1, 1], 1: [0, 0, 0, 0], 2: [0.5, 0.5, 0.5, 0.5], 3: [0.5, 0.5, 1, 1], 4: [0.5, 0.5, 1, 1]}


class GraphError(ValueError):
    pass


def parse_objects(text):
    """v3 files are a sequence of JSON objects; -> {m_ObjectId: obj}, first object (GraphData)."""
    dec, i, objs, first = json.JSONDecoder(), 0, {}, None
    n = len(text)
    while i < n:
        while i < n and text[i].isspace():
            i += 1
        if i >= n:
            break
        o, i = dec.raw_decode(text, i)
        if first is None:
            first = o
        objs[o.get('m_ObjectId')] = o
    if not first or first.get('m_Type') != 'UnityEditor.ShaderGraph.GraphData':
        raise GraphError('not a Shader Graph v3 file (legacy pre-2020 serialization: reopen and save it in Unity 2021+)')
    return objs, first


def short(t):
    return str(t).rsplit('.', 1)[-1]


def _vec(v, width=4):
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return [float(v)] * 4 if width == 1 else [float(v)] + [0.0] * 3
    if isinstance(v, dict):
        if 'r' in v:
            return [float(v.get(k, 0)) for k in 'rgba']
        if 'e00' in v:   # matrix default (dynamic slot): its diagonal is what a vector multiply sees as identity
            return [float(v.get('e00', 0))] * 4
        vals = [float(v.get(k, 0)) for k in 'xyzw' if k in v]
        return vals + [0.0] * (4 - len(vals))
    if isinstance(v, bool):
        return [1.0 if v else 0.0] * 4
    return [0.0] * 4


class Graph:
    def __init__(self, text, path=''):
        self.path = path
        self.objs, self.data = parse_objects(text)
        self.nodes = {n['m_Id']: self.objs.get(n['m_Id']) for n in self.data.get('m_Nodes') or []}
        self.nodes = {k: v for k, v in self.nodes.items() if v}
        self.edges = {}
        for e in self.data.get('m_Edges') or []:
            try:
                out, inn = e['m_OutputSlot'], e['m_InputSlot']
                self.edges[(inn['m_Node']['m_Id'], inn['m_SlotId'])] = (out['m_Node']['m_Id'], out['m_SlotId'])
            except (KeyError, TypeError):
                continue
        self.props = {}
        for p in self.data.get('m_Properties') or []:
            o = self.objs.get(p.get('m_Id'))
            if o:
                self.props[p['m_Id']] = o
        self.blocks = {}
        for ctx in ('m_VertexContext', 'm_FragmentContext'):
            for b in (self.data.get(ctx) or {}).get('m_Blocks') or []:
                o = self.objs.get(b.get('m_Id'))
                if o:
                    self.blocks[o.get('m_SerializedDescriptor') or o.get('m_Name')] = b['m_Id']
                    self.nodes[b['m_Id']] = o
        out = (self.data.get('m_OutputNode') or {}).get('m_Id')
        self.output_node = out if out in self.nodes else None
        self.targets = [self.objs.get(t.get('m_Id')) for t in self.data.get('m_ActiveTargets') or []]
        self.targets = [t for t in self.targets if t]

    def slots(self, node):
        out = {}
        for s in node.get('m_Slots') or []:
            o = self.objs.get(s.get('m_Id'))
            if o is not None:
                out[o.get('m_Id')] = o
        return out

    def settings(self):
        """Surface settings of the URP (or built-in) target: dict(surface, blend, clip, faces, lit, override)."""
        urp = next((t for t in self.targets if short(t['m_Type']) == 'UniversalTarget'), None)
        t = urp or next((t for t in self.targets if short(t['m_Type']) == 'BuiltInTarget'), None) or {}
        sub = self.objs.get((t.get('m_ActiveSubTarget') or {}).get('m_Id')) or {}
        sub_type = short(sub.get('m_Type', ''))
        return dict(surface=t.get('m_SurfaceType', 0), blend=t.get('m_AlphaMode', 0), clip=bool(t.get('m_AlphaClip', False)),
                    faces=t.get('m_RenderFace', 2), lit='Lit' in sub_type and 'Unlit' not in sub_type,
                    unlit='Unlit' in sub_type, decal='Decal' in sub_type, override=bool(t.get('m_AllowMaterialOverride', False)),
                    target=short(t.get('m_Type', 'none')), subTarget=sub_type, vfx=any(short(x['m_Type']) == 'VFXTarget' for x in self.targets))

    def node_types(self):
        return sorted({short(n['m_Type']) for n in self.nodes.values() if short(n['m_Type']) != 'BlockNode'})


class Builder:
    """Emits engine nodes with structural deduplication; values are (engine id, width)."""

    def __init__(self, domain):
        self.domain, self.nodes, self.index, self.notes, self.samples = domain, [], {}, [], 0
        self.unsupported, self.textures = {}, {}

    def add(self, op, inputs=(), **extra):
        if op not in ENGINE_OPS:
            raise GraphError('internal: unknown engine op ' + op)
        key = json.dumps([op, list(inputs), extra], sort_keys=True)
        if key in self.index:
            return self.index[key]
        nid = 'n%d' % len(self.nodes)
        node = dict(id=nid, op=op)
        if inputs:
            node['inputs'] = list(inputs)
        node.update(extra)
        self.nodes.append(node)
        self.index[key] = nid
        return nid

    def const(self, v):
        v = [round(float(x), 6) if math.isfinite(float(x)) else 0.0 for x in v]
        v = [max(-1000000.0, min(1000000.0, x)) for x in v]
        return self.add('constant', value=v[0] if len(set(v)) == 1 else v)

    def note(self, msg):
        if msg not in self.notes:
            self.notes.append(msg)


class Converter:
    def __init__(self, graph, domain, library=None, depth=0):
        self.g, self.domain, self.lib, self.depth = graph, domain, library, depth
        self.b = Builder(domain)
        self.memo = {}
        self.bindings = {}    # subgraph property guid -> (engine id, width) when inlined
        self.flip = None

    # -------------------------------------------------------------- helpers
    def note(self, msg):
        self.b.note(msg)

    def const(self, v, width=4):
        return self.b.const(_vec(v, width) if not isinstance(v, list) else (v + [0.0] * 4)[:4]), width

    def mask(self, val, width):
        """Zero components past `width` (engine ports are always float4)."""
        nid, w = val
        if width >= 4:
            return nid
        m = [1.0 if i < width else 0.0 for i in range(4)]
        return self.b.add('multiply', [nid, self.b.const(m)])

    def scalar(self, val):
        nid, w = val
        return nid if w == 1 else self.b.add('red', [nid])

    def uv_unity(self):
        """Engine UV [u, v_down] -> Unity [u, 1 - v_down]."""
        if self.flip is None:
            uv = self.b.add('uv')
            self.flip = self.b.add('tilingOffset', [uv, self.b.const([1, -1, 1, 1]), self.b.const([0, 1, 0, 0])])
        return self.flip

    def uv_engine(self, nid):
        return self.b.add('tilingOffset', [nid, self.b.const([1, -1, 1, 1]), self.b.const([0, 1, 0, 0])])

    # -------------------------------------------------------------- inputs
    def slot_default(self, node, slot):
        t = short(slot.get('m_Type', ''))
        w = SLOT_WIDTH.get(t, 4)
        if t == 'UVMaterialSlot':
            if slot.get('m_Channel', 0):
                self.note('UV channel %d read as UV0' % slot['m_Channel'])
            return self.uv_unity(), 2
        if t == 'PositionMaterialSlot':
            return self.position(slot.get('m_Space', 0)), 3
        if t == 'NormalMaterialSlot':
            return self.b.add('normal') if self.domain == 'material' else self.b.const([0, 1, 0, 0]), 3
        if t == 'ViewDirectionMaterialSlot':
            return self.b.add('viewDirection') if self.domain != 'postfx' else self.b.const([0, 0, 1, 0]), 3
        if t == 'ScreenPositionMaterialSlot':
            return self.b.add('screenPosition'), 4
        if t == 'VertexColorMaterialSlot':
            return self.b.add('vertexColor'), 4
        if t in ('DynamicVectorMaterialSlot', 'DynamicValueMaterialSlot', 'DynamicMatrixMaterialSlot'):
            v = slot.get('m_Value')
            vals = _vec(v)
            if isinstance(v, dict) and 'e00' not in v:
                w = len([k for k in 'xyzw' if k in v]) or 4
            return self.b.const(vals if w != 1 else [vals[0]] * 4), w
        v = slot.get('m_Value')
        vals = _vec(v, w)
        if w == 1:
            vals = [vals[0]] * 4
        return self.b.const(vals), w

    def inp(self, node, slot_id):
        """(engine id, unity width) feeding input slot `slot_id` of `node`."""
        src = self.g.edges.get((node['m_ObjectId'], slot_id))
        slot = self.g.slots(node).get(slot_id)
        if src is not None:
            val = self.value(src[0], src[1])
            if slot is not None and SLOT_WIDTH.get(short(slot.get('m_Type', ''))) == 1 and val[1] > 1:
                return self.b.add('red', [val[0]]), 1
            return val
        if slot is None:
            return self.b.const([0, 0, 0, 0]), 1
        return self.slot_default(node, slot)

    def has_input(self, node, slot_id):
        return (node['m_ObjectId'], slot_id) in self.g.edges

    def position(self, space):
        if space == 0:
            if self.domain == 'material':
                return self.b.add('objectPosition')
            self.note('object-space position read as world space in the %s domain' % self.domain)
        elif space not in (2, 4):
            self.note('view/tangent-space position read as world space')
        return self.b.add('worldPosition') if self.domain != 'postfx' else self.b.const([0, 0, 0, 1])

    # -------------------------------------------------------------- nodes
    def value(self, node_id, slot_id):
        key = (node_id, slot_id)
        if key in self.memo:
            return self.memo[key]
        if len(self.memo) > 4000:
            raise GraphError('graph too large')
        node = self.g.nodes.get(node_id)
        if node is None:
            out = self.b.const([0, 0, 0, 0]), 4
        else:
            self.memo[key] = (self.b.const([0, 0, 0, 0]), 4)   # cycle guard
            out = self.node(node, slot_id)
        self.memo[key] = out
        return out

    def out_width(self, node, slot_id, dyn):
        slot = self.g.slots(node).get(slot_id) or {}
        t = short(slot.get('m_Type', ''))
        return SLOT_WIDTH.get(t, dyn)

    def node(self, n, slot_id):
        t = short(n['m_Type'])
        b = self.b
        ins = [s for s in self.g.slots(n).values() if s.get('m_SlotType') == 0]
        ins.sort(key=lambda s: s.get('m_Id', 0))

        def a(i):
            return self.inp(n, ins[i]['m_Id']) if i < len(ins) else (b.const([0, 0, 0, 0]), 1)

        def dyn(*vals):
            return max(v[1] for v in vals) if vals else 4

        binary = {'MultiplyNode': 'multiply', 'AddNode': 'add', 'SubtractNode': 'subtract', 'DivideNode': 'divide',
                  'PowerNode': 'pow', 'MaximumNode': 'max', 'MinimumNode': 'min', 'ModuloNode': 'modulo', 'StepNode': 'step'}
        unary = {'OneMinusNode': 'oneMinus', 'SaturateNode': 'clamp', 'AbsoluteNode': 'abs', 'NegateNode': 'negate',
                 'SineNode': 'sin', 'CosineNode': 'cos', 'FractionNode': 'fract', 'FloorNode': 'floor', 'SquareRootNode': 'sqrt'}
        if t in binary:
            x, y = a(0), a(1)
            return b.add(binary[t], [x[0], y[0]]), self.out_width(n, 2, dyn(x, y))
        if t in unary:
            x = a(0)
            return b.add(unary[t], [x[0]]), x[1]
        if t == 'ReciprocalNode':
            x = a(0)
            return b.add('divide', [b.const([1, 1, 1, 1]), x[0]]), x[1]
        if t == 'LerpNode':
            x, y, z = a(0), a(1), a(2)
            return b.add('mix', [x[0], y[0], z[0]]), dyn(x, y)
        if t == 'SmoothstepNode':
            x, y, z = a(0), a(1), a(2)
            return b.add('smoothstep', [x[0], y[0], z[0]]), dyn(x, y, z)
        if t == 'ClampNode':
            x, lo, hi = a(0), a(1), a(2)
            return b.add('clampRange', [x[0], lo[0], hi[0]]), x[1]
        if t == 'RemapNode':
            x, i, o = a(0), a(1), a(2)
            return b.add('remap', [x[0], b.add('red', [i[0]]), b.add('green', [i[0]]), b.add('red', [o[0]]), b.add('green', [o[0]])]), x[1]
        if t == 'DotProductNode':
            x, y = a(0), a(1)
            w = min(dyn(x, y), 4)
            if w == 3:
                return b.add('dot3', [x[0], y[0]]), 1
            return b.add('dot', [self.mask(x, w), self.mask(y, w)]), 1
        if t == 'LengthNode':
            x = a(0)
            return (b.add('length3', [x[0]]) if x[1] == 3 else b.add('length', [self.mask(x, x[1])])), 1
        if t == 'DistanceNode':
            x, y = a(0), a(1)
            w = dyn(x, y)
            return (b.add('distance3', [x[0], y[0]]) if w == 3 else b.add('distance', [self.mask(x, w), self.mask(y, w)])), 1
        if t == 'NormalizeNode':
            x = a(0)
            if x[1] == 3:
                return b.add('normalize3', [x[0]]), 3
            m = self.mask(x, x[1])
            return b.add('divide', [m, b.add('length', [m])]), x[1]
        if t == 'SplitNode':
            x = a(0)
            ch = {1: 'red', 2: 'green', 3: 'blue', 4: 'alpha'}.get(slot_id, 'red')
            return b.add(ch, [x[0]]), 1
        if t == 'CombineNode':
            r, g, bl, al = (self.scalar(a(i)) for i in range(4))
            if slot_id == 5:
                return b.add('combine', [r, g, bl, b.const([1, 1, 1, 1])]), 3
            if slot_id == 6:
                z = b.const([0, 0, 0, 0])
                return b.add('combine', [r, g, z, z]), 2
            return b.add('combine', [r, g, bl, al]), 4
        if t == 'SwizzleNode':
            x = a(0)
            mask = str(n.get('_maskInput') or n.get('convertedMask') or 'xyzw').lower().replace('x', 'r').replace('y', 'g').replace('z', 'b').replace('w', 'a')
            if not re.fullmatch(r'[rgba]{1,4}', mask):
                self.note('swizzle mask %r not understood' % mask)
                return x
            w = len(mask)
            return b.add('swizzle', [x[0]], mask=(mask + mask[-1] * 4)[:4]), w
        if t == 'BranchNode':
            p, tv, fv = a(0), a(1), a(2)
            return b.add('branch', [p[0], tv[0], fv[0]]), dyn(tv, fv)
        if t == 'ComparisonNode':
            x, y = a(0), a(1)
            kind = n.get('m_ComparisonType', 0)
            op = {0: 'equal', 1: 'equal', 2: 'lessThan', 3: 'greaterThan', 4: 'greaterThan', 5: 'lessThan'}.get(kind, 'equal')
            r = b.add(op, [self.scalar(x), self.scalar(y)])
            return (b.add('oneMinus', [r]) if kind in (1, 3, 5) else r), 1
        if t == 'BooleanNode':
            return b.const([1.0 if n.get('m_Value') else 0.0] * 4), 1
        if t in ('Vector1Node', 'Vector2Node', 'Vector3Node', 'Vector4Node'):
            w = {'Vector1Node': 1, 'Vector2Node': 2, 'Vector3Node': 3, 'Vector4Node': 4}[t]
            comps = [self.scalar(a(i)) for i in range(w)]
            if w == 1:
                return comps[0], 1
            z = b.const([0, 0, 0, 0])
            return b.add('combine', (comps + [z] * 4)[:4]), w
        if t == 'ColorNode':
            c = n.get('m_Color') or {}
            col = _vec(c.get('color') if isinstance(c, dict) else c)
            if isinstance(c, dict) and c.get('mode', 0) == 0:
                col = [_lin(x) for x in col[:3]] + col[3:]
            return b.const(col), 4
        if t == 'PropertyNode':
            return self.property(n)
        if t in ('SampleTexture2DNode', 'SampleTexture2DLODNode'):
            return self.sample(n, slot_id, lod=t.endswith('LODNode'))
        if t == 'UVNode':
            if n.get('m_OutputChannel', 0):
                self.note('UV channel %d read as UV0' % n['m_OutputChannel'])
            return self.uv_unity(), 2
        if t == 'TilingAndOffsetNode':
            uv, til, off = a(0), a(1), a(2)
            return b.add('tilingOffset', [uv[0], til[0], off[0]]), 2
        if t == 'TimeNode':
            tm = b.add('time')
            return {0: tm, 1: b.add('sin', [tm]), 2: b.add('cos', [tm])}.get(slot_id, b.const([1 / 60.0] * 4)), 1
        if t == 'VertexColorNode':
            if self.domain == 'postfx':
                return b.const([1, 1, 1, 1]), 4
            return b.add('vertexColor'), 4
        if t == 'PositionNode':
            return self.position(n.get('m_Space', 0)), 3
        if t == 'NormalVectorNode':
            if self.domain != 'material':
                self.note('normal vector read as +Y outside the material domain')
                return b.const([0, 1, 0, 0]), 3
            if n.get('m_Space', 2) != 2:
                self.note('object/tangent-space normal read as world space')
            return b.add('normal'), 3
        if t == 'ViewDirectionNode':
            if self.domain == 'postfx':
                return b.const([0, 0, 1, 0]), 3
            return b.add('viewDirection'), 3
        if t == 'FresnelNode':
            nrm, view, pw = a(0), a(1), a(2)
            return b.add('fresnel', [nrm[0], view[0], self.scalar(pw)]), 1
        if t == 'ScreenPositionNode':
            mode = n.get('m_ScreenSpaceType', 0)
            if mode not in (0,):
                self.note('screen position mode %d read as normalized (Default)' % mode)
            return b.add('screenPosition'), 4
        if t == 'PolarCoordinatesNode':
            uv, center, rs, ls = a(0), a(1), a(2), a(3)
            d = self.mask((b.add('subtract', [uv[0], center[0]]), 2), 2)
            radius = b.add('multiply', [b.add('length', [d]), b.add('multiply', [self.scalar(rs), b.const([2, 2, 2, 2])])])
            ang = b.add('atan2', [b.add('red', [d]), b.add('green', [d])])
            ang = b.add('multiply', [ang, b.add('multiply', [self.scalar(ls), b.const([1 / 6.28] * 4)])])
            z = b.const([0, 0, 0, 0])
            return b.add('combine', [radius, ang, z, b.const([1, 1, 1, 1])]), 2
        if t == 'TwirlNode':
            uv, center, strength, off = a(0), a(1), a(2), a(3)
            d = self.mask((b.add('subtract', [uv[0], center[0]]), 2), 2)
            ang = b.add('multiply', [self.scalar(strength), b.add('length', [d])])
            c, s = b.add('cos', [ang]), b.add('sin', [ang])
            dx, dy = b.add('red', [d]), b.add('green', [d])
            x = b.add('subtract', [b.add('multiply', [c, dx]), b.add('multiply', [s, dy])])
            y = b.add('add', [b.add('multiply', [s, dx]), b.add('multiply', [c, dy])])
            z = b.const([0, 0, 0, 0])
            rot = b.add('combine', [x, y, z, z])
            return b.add('add', [b.add('add', [rot, self.mask(center, 2)]), self.mask(off, 2)]), 2
        if t == 'RadialShearNode':
            uv, center, strength, off = a(0), a(1), a(2), a(3)
            d = self.mask((b.add('subtract', [uv[0], center[0]]), 2), 2)
            d2 = b.add('dot', [d, d])
            amount = b.add('multiply', [d2, self.mask(strength, 2)])
            perp = b.add('multiply', [b.add('swizzle', [d], mask='grba'), b.const([1, -1, 0, 0])])
            return b.add('add', [b.add('add', [uv[0], b.add('multiply', [perp, amount])]), self.mask(off, 2)]), 2
        if t == 'RotateNode':
            uv, center, rot = a(0), a(1), a(2)
            ang = self.scalar(rot)
            if n.get('m_Unit', 0) == 1:
                ang = b.add('radians', [ang])
            # Unity Rotate turns clockwise; engine rotateUV counter-clockwise
            return b.add('rotateUV', [uv[0], center[0], b.add('negate', [ang])]), 2
        if t == 'RectangleNode':
            if self.domain == 'material' and self.vertex:
                raise GraphError('Rectangle (screen derivatives) on the vertex path')
            uv, wd, ht = a(0), a(1), a(2)
            d = b.add('subtract', [b.add('abs', [b.add('subtract', [b.add('multiply', [uv[0], b.const([2, 2, 2, 2])]), b.const([1, 1, 1, 1])])]),
                                   b.add('combine', [self.scalar(wd), self.scalar(ht), b.const([0, 0, 0, 0]), b.const([0, 0, 0, 0])])])
            d = b.add('oneMinus', [b.add('divide', [d, b.add('fwidth', [d])])])
            return b.add('clamp', [b.add('min', [b.add('red', [d]), b.add('green', [d])])]), 1
        if t == 'SphereMaskNode':
            co, ce, ra, ha = a(0), a(1), a(2), a(3)
            w = dyn(co, ce)
            dist = b.add('distance3', [co[0], ce[0]]) if w == 3 else b.add('distance', [self.mask(co, w), self.mask(ce, w)])
            v = b.add('divide', [b.add('subtract', [dist, self.scalar(ra)]), b.add('oneMinus', [self.scalar(ha)])])
            return b.add('oneMinus', [b.add('clamp', [v])]), 1
        if t in ('NoiseNode', 'GradientNoiseNode'):
            uv, sc = a(0), a(1)
            p = b.add('multiply', [uv[0], self.scalar(sc)])
            self.note('%s approximated by the engine %s (single octave, different hash)' % (t, 'value noise' if t == 'NoiseNode' else 'gradient noise'))
            return b.add('noise' if t == 'NoiseNode' else 'gradientNoise', [p]), 1
        if t == 'VoronoiNode':
            uv, _, dens = a(0), a(1), a(2)
            self.note('Voronoi approximated (no angle offset, cells output = distance)')
            return b.add('voronoi', [b.add('multiply', [uv[0], self.scalar(dens)])]), 1
        if t == 'NormalStrengthNode':
            x, s = a(0), a(1)
            return b.add('normalStrength', [x[0], self.scalar(s)]), 3
        if t == 'NormalUnpackNode':
            return b.add('normalUnpack', [a(0)[0]]), 3
        if t == 'Texture2DPropertiesNode':
            self.note('texel size read as 1/1024')
            return b.const([1 / 1024.0] * 4), 1
        if t == 'SplitTextureTransformNode':
            tex = self.texture_input(n, ins[0]['m_Id']) if ins else None
            if slot_id == 1 or slot_id == 2:
                ref = tex.get('ref') if tex else None
                return b.add('parameter', name='%s_ST_%s' % (ref or 'tex', 'tiling' if slot_id == 1 else 'offset'),
                             value=[1, 1, 0, 0] if slot_id == 1 else 0), 2
            return b.const([0, 0, 0, 0]), 4
        if t in ('RedirectNodeData', 'PreviewNode'):
            return a(0)
        if t == 'IsFrontFaceNode':
            self.note('Is Front Face reads as true')
            return b.const([1, 1, 1, 1]), 1
        if t == 'SubGraphNode':
            return self.subgraph(n, slot_id)
        if t == 'SubGraphOutputNode':
            return self.inp(n, slot_id)
        if t in UNSUPPORTED:
            v, why = UNSUPPORTED[t]
            if t == 'CustomFunctionNode':
                src = n.get('m_FunctionSource') or ''
                where = ' (%s)' % src if src and n.get('m_SourceType', 0) == 0 else ' (inline body)'
                self.b.unsupported[t + ':' + str(n.get('m_FunctionName', '?'))] = 'custom HLSL function %s%s: manual port' % (n.get('m_FunctionName', '?'), where)
            else:
                self.b.unsupported[t] = why
            if v is None:
                return a(0) if ins else (b.const([0, 0, 0, 0]), 4)
            return b.const([v] * 4), self.out_width(n, slot_id, 4)
        self.b.unsupported[t] = 'no engine equivalent'
        return b.const([1, 1, 1, 1]), 4

    vertex = False

    def property(self, n):
        pid = (n.get('m_Property') or {}).get('m_Id')
        prop = self.g.props.get(pid) or self.g.objs.get(pid) or {}
        t = short(prop.get('m_Type', ''))
        guid = (prop.get('m_Guid') or {}).get('m_GuidSerialized')
        if guid in self.bindings:
            return self.bindings[guid]
        ref = prop.get('m_OverrideReferenceName') or prop.get('m_DefaultReferenceName') or prop.get('m_Name') or 'p'
        if t in PROPERTY_WIDTH:
            w = PROPERTY_WIDTH[t]
            v = prop.get('m_Value')
            vals = _vec(v, w)
            if t == 'ColorShaderProperty':
                vals = [_lin(x) for x in vals[:3]] + vals[3:]
            if w == 1:
                vals = [vals[0]] * 4
            return self.b.add('parameter', name=_param_name(ref), value=vals[0] if w == 1 else [round(x, 6) for x in vals],
                              unity=dict(ref=ref, kind=t.replace('ShaderProperty', ''))), w
        if t in ('Texture2DShaderProperty',):
            return self.b.const([0, 0, 0, 0]), 4   # only meaningful through texture_input
        self.b.unsupported['property:' + t] = '%s property %s' % (t, ref)
        return self.b.const([0, 0, 0, 0]), 4

    def texture_input(self, n, slot_id):
        """-> dict(ref=property reference, default=guid, type=default type) for a texture input slot."""
        src = self.g.edges.get((n['m_ObjectId'], slot_id))
        if src:
            sn = self.g.nodes.get(src[0]) or {}
            st = short(sn.get('m_Type', ''))
            if st == 'PropertyNode':
                prop = self.g.props.get((sn.get('m_Property') or {}).get('m_Id')) or {}
                guid = (prop.get('m_Guid') or {}).get('m_GuidSerialized')
                if guid in self.bindings and isinstance(self.bindings[guid], dict):
                    return self.bindings[guid]
                ref = prop.get('m_OverrideReferenceName') or prop.get('m_DefaultReferenceName') or prop.get('m_Name')
                return dict(ref=ref, default=_texture_guid(prop.get('m_Value')), type=prop.get('m_DefaultType', 0))
            if st in ('SplitTextureTransformNode', 'RedirectNodeData'):
                ins = sorted([s for s in self.g.slots(sn).values() if s.get('m_SlotType') == 0], key=lambda s: s.get('m_Id', 0))
                return self.texture_input(sn, ins[0]['m_Id']) if ins else None
            if st == 'SubGraphNode':
                self.note('texture output of a sub graph is not followed')
                return None
            return None
        slot = self.g.slots(n).get(slot_id) or {}
        return dict(ref=None, default=_texture_guid(slot.get('m_Texture')), type=slot.get('m_DefaultType', 0))

    def sample(self, n, slot_id, lod=False):
        b = self.b
        slots = self.g.slots(n)
        by_name = {s.get('m_DisplayName'): s.get('m_Id') for s in slots.values() if s.get('m_SlotType') == 0}
        tex = self.texture_input(n, by_name.get('Texture', 1)) or dict(ref=None, default=None, type=0)
        uv = self.inp(n, by_name['UV']) if 'UV' in by_name else (self.uv_unity(), 2)
        coord = self.uv_engine(uv[0])
        key = tex.get('ref') or ('guid:' + str(tex.get('default')))
        placeholder = '{texture:%s}' % key
        b.textures[key] = dict(tex, normal=n.get('m_TextureType', 0) == 1)
        b.samples += 1
        if lod and self.domain == 'material':
            lv = self.inp(n, by_name['LOD']) if 'LOD' in by_name else (b.const([0, 0, 0, 0]), 1)
            s = b.add('sampleTextureLod', [coord, self.scalar(lv)], texture=placeholder)
        else:
            s = b.add('sampleTexture', [coord], texture=placeholder)
        if n.get('m_TextureType', 0) == 1:
            s = b.add('normalUnpack', [s])
            rgba = (s, 3)
        else:
            rgba = (s, 4)
        ch = {4: 'red', 5: 'green', 6: 'blue', 7: 'alpha'}.get(slot_id)
        return (b.add(ch, [s]), 1) if ch else rgba

    def subgraph(self, n, slot_id):
        if self.depth >= 4 or not self.lib:
            self.b.unsupported['SubGraphNode'] = 'nested sub graph not resolved'
            return self.b.const([0, 0, 0, 0]), 4
        try:
            ref = json.loads(n.get('m_SerializedSubGraph') or '{}').get('subGraph') or {}
        except ValueError:
            ref = {}
        sub = self.lib.load(ref.get('guid', ''))
        if sub is None:
            p = self.lib.ctx.assets.path({'guid': ref.get('guid', '')})
            self.b.unsupported['SubGraphNode:' + str(ref.get('guid', '?'))] = (
                'sub graph %s uses the legacy (pre-2020) serialization: resave it in Unity, then re-import' % Path(p).name if p
                else 'sub graph %s not found' % ref.get('guid', '?'))
            return self.b.const([0, 0, 0, 0]), 4
        inner = Converter(sub, self.domain, self.lib, self.depth + 1)
        inner.b = self.b     # emit into the same graph
        inner.flip = self.flip
        inner.vertex = self.vertex
        for guid, sid in zip(n.get('m_PropertyGuids') or [], n.get('m_PropertyIds') or []):
            prop = next((p for p in sub.props.values() if (p.get('m_Guid') or {}).get('m_GuidSerialized') == guid), None)
            if prop is not None and short(prop.get('m_Type', '')) == 'Texture2DShaderProperty':
                inner.bindings[guid] = self.texture_input(n, sid) or dict(ref=None, default=None, type=0)
            else:
                inner.bindings[guid] = self.inp(n, sid)
        if not sub.output_node:
            return self.b.const([0, 0, 0, 0]), 4
        outn = sub.nodes[sub.output_node]
        slot = sub.slots(outn).get(slot_id)
        if slot is None:   # match by display name
            mine = self.g.slots(n).get(slot_id, {}).get('m_DisplayName')
            slot = next((s for s in sub.slots(outn).values() if s.get('m_DisplayName') == mine), None)
        if slot is None:
            return self.b.const([0, 0, 0, 0]), 4
        r = inner.inp(outn, slot['m_Id'])
        self.flip = inner.flip or self.flip
        return r

    # -------------------------------------------------------------- outputs
    def block_value(self, name):
        bid = self.g.blocks.get(name)
        if bid is None:
            return None
        node = self.g.nodes[bid]
        slots = self.g.slots(node)
        if not slots:
            return None
        sid = next(iter(slots))
        return self.inp(node, sid), self.has_input(node, sid), node, sid

    def convert(self):
        b, g = self.b, self.g
        s = g.settings()
        outputs = {}
        base = self.block_value('SurfaceDescription.BaseColor')
        alpha = self.block_value('SurfaceDescription.Alpha')
        col = base[0][0] if base else b.const([0.5, 0.5, 0.5, 1])
        if alpha and (s['surface'] == 1 or s['clip'] or self.domain == 'particle'):
            col = b.add('setAlpha', [col, self.scalar(alpha[0])])
        else:
            col = b.add('setAlpha', [col, b.const([1, 1, 1, 1])])
        outputs['color'] = col
        if self.domain == 'material':
            em = self.block_value('SurfaceDescription.Emission')
            if em and em[1]:
                outputs['emission'] = em[0][0]
            if s['lit']:
                sm = self.block_value('SurfaceDescription.Smoothness')
                if sm and sm[1]:
                    outputs['roughness'] = b.add('oneMinus', [self.scalar(sm[0])])
                me = self.block_value('SurfaceDescription.Metallic')
                if me and me[1]:
                    outputs['metallic'] = self.scalar(me[0])
                for blk in ('SurfaceDescription.NormalTS', 'SurfaceDescription.NormalWS', 'SurfaceDescription.NormalOS'):
                    nb = self.block_value(blk)
                    if nb and nb[1]:
                        self.note('%s: graph normals are not converted (engine graph normal output is world space without a tangent transform)' % blk.split('.')[1])
                oc = self.block_value('SurfaceDescription.Occlusion')
                if oc and oc[1]:
                    self.note('Occlusion output dropped')
            pos = self.block_value('VertexDescription.Position')
            if pos and pos[1]:
                self.vertex = True
                try:
                    off = b.add('subtract', [pos[0][0], b.add('objectPosition')])
                    if self.vertex_ok(off):
                        outputs['vertexOffset'] = off
                    else:
                        self.note('vertex position uses fragment-only nodes (textures without LOD, screen position): dropped')
                except GraphError as e:
                    self.note('vertex position dropped: %s' % e)
                self.vertex = False
            vn = self.block_value('VertexDescription.Normal')
            if vn and vn[1]:
                self.note('vertex normal output dropped')
        else:
            em = self.block_value('SurfaceDescription.Emission')
            if em and em[1]:
                outputs['color'] = b.add('add', [outputs['color'], b.add('multiply', [em[0][0], b.const([1, 1, 1, 0])])])
                self.note('emission added to the particle colour')
            pos = self.block_value('VertexDescription.Position')
            if pos and pos[1]:
                self.note('vertex displacement is not available to %s graphs: dropped' % self.domain)
        clip = self.block_value('SurfaceDescription.AlphaClipThreshold')
        cutoff = None
        if clip and s['clip']:
            node = next((x for x in b.nodes if x['id'] == clip[0][0][0]), None)
            if node and node['op'] in ('constant', 'parameter'):
                v = node.get('value')
                cutoff = dict(value=v if not isinstance(v, list) else v[0], param=node.get('name') if node['op'] == 'parameter' else None)
            else:
                self.note('computed alpha clip threshold read as 0.5')
                cutoff = dict(value=0.5, param=None)
        return outputs, s, cutoff

    def vertex_ok(self, root):
        by = {x['id']: x for x in self.b.nodes}
        seen, stack = set(), [root]
        while stack:
            nid = stack.pop()
            if nid in seen:
                continue
            seen.add(nid)
            node = by[nid]
            if not ENGINE_OPS[node['op']][2] or (node['op'] in ('objectPosition', 'normal') and self.domain != 'material'):
                return False
            stack += node.get('inputs', [])
        return True


def _lin(c):
    c = float(c)
    return round(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4, 6)


def _param_name(ref):
    s = re.sub(r'[^A-Za-z0-9_]', '_', str(ref))[:60]
    return s or 'p'


def _texture_guid(v):
    if isinstance(v, dict):
        ser = v.get('m_SerializedTexture')
        if isinstance(ser, str):
            try:
                return ((json.loads(ser) or {}).get('texture') or {}).get('guid')
            except ValueError:
                return None
    return None


def prune(nodes, outputs):
    by = {n['id']: n for n in nodes}
    keep, stack = set(), list(outputs.values())
    while stack:
        nid = stack.pop()
        if nid in keep or nid not in by:
            continue
        keep.add(nid)
        stack += by[nid].get('inputs', [])
    return [n for n in nodes if n['id'] in keep]


def convert_text(text, domain='material', path='', library=None):
    """-> template dict(graph, settings, cutoff, textures, notes, unsupported, nodeTypes)."""
    g = Graph(text, path)
    c = Converter(g, domain, library)
    outputs, settings, cutoff = c.convert()
    nodes = prune(c.b.nodes, outputs)
    graph = dict(version=1, domain=domain, nodes=nodes, outputs=outputs)
    used = {n['texture'] for n in nodes if 'texture' in n}
    tex = {k: v for k, v in c.b.textures.items() if '{texture:%s}' % k in used}
    return dict(graph=graph, settings=settings, cutoff=cutoff, textures=tex, notes=c.b.notes,
                unsupported=dict(sorted(c.b.unsupported.items())), nodeTypes=g.node_types(), source=path)


def validate(graph):
    """Structural check mirroring engine/render/graph (arity, domain, limits, cycles). -> list of errors."""
    errs = []
    nodes = graph.get('nodes', [])
    bit = DOMAIN_BIT.get(graph.get('domain'), 0)
    ids = {n['id'] for n in nodes}
    if len(nodes) > MAX_NODES:
        errs.append('%d nodes (limit %d)' % (len(nodes), MAX_NODES))
    samples = sum(1 for n in nodes if n['op'] in ('texture', 'sampleTexture', 'sampleTextureLod'))
    files = {n['texture'] for n in nodes if n['op'] in ('sampleTexture', 'sampleTextureLod')}
    if samples > MAX_SAMPLES:
        errs.append('%d texture samples (limit %d)' % (samples, MAX_SAMPLES))
    if len(files) > MAX_TEXTURES:
        errs.append('%d texture files (limit %d)' % (len(files), MAX_TEXTURES))
    for n in nodes:
        spec = ENGINE_OPS.get(n['op'])
        if not spec:
            errs.append('%s: unknown op %s' % (n['id'], n['op']))
            continue
        if len(n.get('inputs', [])) != spec[0]:
            errs.append('%s: %s takes %d inputs' % (n['id'], n['op'], spec[0]))
        if not spec[1] & bit:
            errs.append('%s: %s not available in the %s domain' % (n['id'], n['op'], graph.get('domain')))
        for i in n.get('inputs', []):
            if i not in ids:
                errs.append('%s: missing input %s' % (n['id'], i))
    if 'color' not in graph.get('outputs', {}):
        errs.append('no color output')
    return errs


class Library:
    """Converted templates by .shadergraph path, sub graphs by GUID; per-material instantiation."""

    def __init__(self, ctx):
        self.ctx = ctx
        self.cache, self.sub_cache, self.guid_paths = {}, {}, None
        self.report = {}   # path -> template summary

    def load(self, guid):
        if guid in self.sub_cache:
            return self.sub_cache[guid]
        g = None
        p = self.ctx.assets.path({'guid': guid})
        if p and p.endswith(('.shadersubgraph', '.shadergraph')):
            try:
                g = Graph(Path(p).read_text(encoding='utf-8-sig', errors='replace'), p)
            except (GraphError, ValueError, OSError):
                g = None
        self.sub_cache[guid] = g
        return g

    def template(self, rel, domain):
        key = (rel, domain)
        if key not in self.cache:
            try:
                tpl = convert_text((self.ctx.root / rel).read_text(encoding='utf-8-sig', errors='replace'), domain, rel, self)
                errs = validate(tpl['graph'])
                if errs:
                    tpl['error'] = '; '.join(errs[:5])
            except (GraphError, ValueError, OSError, KeyError, TypeError, RecursionError) as e:
                tpl = dict(error=str(e) or type(e).__name__, source=rel, unsupported={}, notes=[], nodeTypes=[])
            self.cache[key] = tpl
            self.report[rel + '#' + domain] = {k: tpl.get(k) for k in ('error', 'unsupported', 'notes', 'nodeTypes', 'settings')}
            if 'graph' in tpl:
                self.report[rel + '#' + domain]['nodes'] = len(tpl['graph']['nodes'])
        return self.cache[key]

    def apply(self, rel, mp, domain='material'):
        """Fill mp.r.material from the graph; False when the graph could not be converted."""
        tpl = self.template(rel, domain)
        res = mp.r
        if 'graph' not in tpl or tpl.get('error'):
            res.graph = dict(source=rel, error=tpl.get('error'))
            return False
        graph, textures = instantiate(tpl, mp)
        m = res.material
        s = tpl['settings']
        m['shaderGraph'] = graph
        m['vertexColors'] = False
        if s['unlit'] or not s['lit']:
            m['unlit'] = True
        p = mp.p
        if s['override'] and '_Surface' in p.floats:
            mp.surface()
        else:
            if s['surface'] == 1:
                m['alpha'] = 'blend'
                m['castShadow'] = False
                if s['blend'] in (2, 3):
                    res.note('approx', '%s blending of the graph target has no model material equivalent.' % ('Additive' if s['blend'] == 2 else 'Multiply'))
            if s['faces'] == 0:
                m['doubleSided'] = True
        if s['clip'] and tpl.get('cutoff') and m.get('alpha') != 'blend':
            c = tpl['cutoff']
            v = p.f(c['param'] if c.get('param') else '', None) if c.get('param') else None
            m['alpha'] = 'mask'
            m['cutoff'] = round(float(v if v is not None else c['value']), 4)
        for rel_tex, role in textures:
            if (rel_tex, role) not in res.textures:
                res.textures.append((rel_tex, role))
        for why in tpl['unsupported'].values():
            res.note('missing' if 'manual' in why else 'approx', 'Shader Graph %s: %s.' % (Path(rel).name, why))
        for n in tpl['notes']:
            res.note('approx', 'Shader Graph %s: %s.' % (Path(rel).name, n))
        if s.get('decal'):
            res.note('missing', 'Decal Shader Graph %s: projected decals are not models.' % Path(rel).name)
        res.graph = dict(source=rel, nodes=len(graph['nodes']))
        return True


def instantiate(tpl, mp):
    """Copy of the template with the material's parameter values and texture paths. -> (graph, [(rel, role)])."""
    graph = json.loads(json.dumps(tpl['graph']))
    p, ctx = mp.p, mp.ctx
    params, used = {}, []
    for n in graph['nodes']:
        u = n.pop('unity', None)
        if n['op'] == 'parameter' and u:
            ref, kind = u['ref'], u['kind']
            if kind in ('Vector1', 'Boolean') and ref in p.floats:
                params[n['name']] = round(float(p.f(ref, 0) or 0), 6)
            elif kind == 'Color' and p.color(ref):
                c = p.color(ref)
                params[n['name']] = [_lin(x) if ctx.linear else round(x, 6) for x in c[:3]] + [round(c[3], 6)]
            elif kind.startswith('Vector') and p.color(ref):
                params[n['name']] = [round(x, 6) for x in p.color(ref)]
        elif n['op'] in ('sampleTexture', 'sampleTextureLod'):
            key = n['texture'][len('{texture:'):-1]
            info = tpl['textures'].get(key, {})
            rel = ''
            ref = info.get('ref')
            if ref and p.texref(ref):
                rel = ctx.rel(p.texref(ref))
            elif info.get('default'):
                rel = ctx.rel({'guid': info['default']})
            if rel and Path(rel).suffix.lower() in ('.png', '.tga', '.jpg', '.jpeg', '.psd', '.tif', '.tiff', '.bmp'):
                role = 'normal' if info.get('normal') else 'base'
                ti = (ctx.meta(rel) or {}).get('TextureImporter') or {}
                srgb = ((ti.get('mipmaps') or {}).get('sRGBTexture', 1) == 1) and not info.get('normal') and ti.get('textureType') != 1
                n['texture'] = 'content/' + rel
                n['srgb'] = bool(srgb)
                used.append((rel, role if srgb or role == 'normal' else 'data'))
            else:
                nid = n['id']
                n.clear()   # the material has no texture there: Unity's default (white / black / grey / bump)
                n.update(id=nid, op='constant', value=DEFAULT_TEXTURE.get(info.get('type', 0), [1, 1, 1, 1]))
    if params:
        graph['parameters'] = params
    return graph, used


def detail_graph(detail, mask, base_s, base_o, ds, do, scale=1.0):
    """URP Lit detail albedo: albedo * lerp(1, 2 * detail.rgb, mask.a) (scaled when _DetailAlbedoMapScale != 1).

    sg_uv already carries the base _ST (uvScale / uvOffset, engine V-down space), so the detail UV is
    sg_uv * (detailScale / baseScale) + engineOffset(detail) - engineOffset(base) * detailScale / baseScale."""
    def eng_off(s, o):
        return [o[0], 1 - s[1] - o[1]]
    k = [ds[0] / (base_s[0] or 1), ds[1] / (base_s[1] or 1)]
    bo, dof = eng_off(base_s, base_o), eng_off(ds, do)
    off = [dof[0] - bo[0] * k[0], dof[1] - bo[1] * k[1]]
    nodes = [dict(id='in', op='input'), dict(id='uv', op='uv'),
             dict(id='til', op='constant', value=[round(k[0], 6), round(k[1], 6), 1, 1]),
             dict(id='off', op='constant', value=[round(off[0], 6), round(off[1], 6), 0, 0]),
             dict(id='duv', op='tilingOffset', inputs=['uv', 'til', 'off']),
             dict(id='detail', op='sampleTexture', inputs=['duv'], texture=detail, srgb=False),
             dict(id='two', op='constant', value=2.0 if scale == 1 else 2.0 * scale),
             dict(id='x2', op='multiply', inputs=['detail', 'two'])]
    if scale != 1:   # ScaleDetailAlbedo: lerp(0.5, x, scale) * 2 relative to grey
        nodes[-2] = dict(id='two', op='constant', value=2.0)
        nodes += [dict(id='half', op='constant', value=1.0), dict(id='sc', op='constant', value=round(scale, 6)),
                  dict(id='x2s', op='mix', inputs=['half', 'x2', 'sc'])]
    x2 = 'x2s' if scale != 1 else 'x2'
    nodes.append(dict(id='one', op='constant', value=1.0))
    if mask:
        nodes += [dict(id='muv', op='uv'), dict(id='mask', op='sampleTexture', inputs=['muv'], texture=mask),
                  dict(id='ma', op='alpha', inputs=['mask'])]
        nodes.append(dict(id='mul', op='mix', inputs=['one', x2, 'ma']))
    else:
        nodes.append(dict(id='mul', op='mix', inputs=['one', x2, 'one']))
    nodes += [dict(id='rgb', op='rgb', inputs=['mul']), dict(id='out', op='multiply', inputs=['in', 'rgb'])]
    return dict(version=1, domain='material', nodes=nodes, outputs={'color': 'out'})


def coverage():
    return dict(supported=SUPPORTED, unsupported={k: v[1] for k, v in UNSUPPORTED.items()})
