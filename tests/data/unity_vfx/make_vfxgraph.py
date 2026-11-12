#!/usr/bin/env python3
"""Writes the synthetic VFX Graph of this Unity test project (Assets/FX/VfxGraph/FireGraph.vfx), hand-built here in
Unity's text serialization (nothing taken from a real project). Node scripts use the GUID prefixes of
com.unity.visualeffectgraph that tools/importers/unity_vfx/vfxgraph_model.py knows; the rest of each GUID is filler.

    python3 tests/data/unity_vfx/make_vfxgraph.py

Graph: "Embers" (spawner: constant rate from the exposed property EmberRate + a burst of 5; initialize: random
lifetime, size, colour, sphere surface shape, velocity from direction & speed (spherical); update: gravity, turbulence,
trigger event on die -> GPU event; output: quad, along velocity, texture spark.png, colour gradient and size curve over
life (size curve * age via Sample Curve)) and "Sparks" (started by the GPU event: burst 3, life 0.4, quad output).
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'Assets', 'FX', 'VfxGraph', 'FireGraph.vfx')
SPARK_GUID = None   # read from Textures/spark.png.meta

FLOAT = 'System.Single, mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089'
UINT = 'System.UInt32, mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089'
VEC3 = 'UnityEngine.Vector3, UnityEngine.CoreModule, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
COLOR = 'UnityEngine.Color, UnityEngine.CoreModule, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
CURVE = 'UnityEngine.AnimationCurve, UnityEngine.CoreModule, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
GRAD = 'UnityEngine.Gradient, UnityEngine.CoreModule, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
TEX = 'UnityEngine.Texture2D, UnityEngine.CoreModule, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
SPHERE = 'UnityEditor.VFX.TArcSphere, Unity.VisualEffectGraph.Editor, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
GPUEVT = 'UnityEditor.VFX.GPUEvent, Unity.VisualEffectGraph.Editor, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'
VECTOR = 'UnityEditor.VFX.Vector, Unity.VisualEffectGraph.Editor, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'

G = {   # class -> script guid (prefix known to the importer + filler)
    'graph': '7d4c867f6b72b714dbb5fd1780afe208', 'spawner': '73a13919d81fb7444849bae8b5c812a2',
    'init': '9dfea48843f53fc438eabc12a3a30abc', 'update': '2dc095764ededfa4bb32fa602511ea4b',
    'quad': 'a0b9e6b9139e58d4c957ec54595da7d3', 'gpuevent': 'f42a6449da2296343af0d8536de8588a',
    'dataParticle': 'd78581a96eae8bf4398c282eb0b098bd', 'dataSpawner': 'f68759077adc0b143b6e1c101e82065e',
    'rate': 'f05c6884b705ce14d82ae720f0ec209f', 'burst': '5e382412bb691334bb79457a6c127924',
    'set': 'a971fa2e110a0ac42ac1d8dae408704b', 'curve': '01ec2c1930009b04ea08905b47262415',
    'shape': 'fb1f6794ace8b0c4592af9c5604cddbf', 'veldir': '92b80e8c00000000000000000000aaaa',
    'gravity': 'e5dce54a00000000000000000000aaaa', 'turb': '63716c0daf1806941a123003dc6d7398',
    'trigger': '9a7ad76700000000000000000000aaaa', 'orient': 'd16c6aeaef944094b9a1633041804207',
    'param': '330e0fca1717dde4aaa144f48232aa64', 'random': 'c42128e17c583714a909b4997c80c916',
    'sampleCurve': 'f8bcc906a6d398c46b18826714448709', 'age': 'ba941214d319b454f90d5480e85886f2',
    'mul': 'b8ee8a7543fa09e42a7c8616f60d2ad7',
    'sFloat': 'f780aa281814f9842a7c076d436932e7', 'sUint': 'c52d920e7fff73b498050a6b3c4404ca',
    'sVec3': 'ac39bd03fca81b849929b9c966f1836a', 'sColor': 'c82227d5759e296488798b1554a72a15',
    'sCurve': 'c117b74c5c58db542bffe25c78fe92db', 'sGrad': '76f778ff57c4e8145b9681fe3268d8e9',
    'sTex': '70a331b1d86cc8d4aa106ccbe0da5852', 'sStruct': '1b605c022ee79394a8a776c0869b3f9a',
    'sVector': 'a9f9544b71b70000000000000000aaaa', 'sUndefined': '388ad3b1dc9c6ae45b630f914fab638f',
}

docs = []
next_id = [8926484042661614600]


def nid():
    next_id[0] += 1
    return next_id[0]


def node(cls, fields):
    i = nid()
    docs.append((i, cls, fields))
    return i


def ref(i):
    return '{fileID: %d}' % i


def slot(owner, name, stype, value, sguid, parent=0, master=None, linked=(), direction=0, children=()):
    i = nid()
    m = master or i
    docs.append((i, sguid, {
        'm_Parent': ref(parent), 'm_Children': list(children), 'm_UIPosition': '{x: 0, y: 0}',
        'm_MasterSlot': ref(m),
        '_master': (owner if m == i else 0, stype, value) if m == i else None,
        'm_Property': (name, stype), 'm_Direction': direction, 'm_LinkedSlots': list(linked)}))
    return i


def write():
    lines = ['%YAML 1.1', '%TAG !u! tag:unity3d.com,2011:',
             '--- !u!2058629511 &8926484042661614527', 'VisualEffectResource:', '  m_ObjectHideFlags: 0',
             '  m_Name: FireGraph', '  m_Graph: {fileID: %d}' % GRAPH]
    for i, cls, f in docs:
        g = G.get(cls, cls)
        lines += ['--- !u!114 &%d' % i, 'MonoBehaviour:', '  m_ObjectHideFlags: 0', '  m_GameObject: {fileID: 0}',
                  '  m_Enabled: 1', '  m_Script: {fileID: 11500000, guid: %s, type: 3}' % g, '  m_Name: ']
        for k, v in f.items():
            if k == '_master':
                if v is not None:
                    owner, stype, value = v
                    lines += ['  m_MasterData:', '    m_Owner: {fileID: %d}' % owner, '    m_Value:', '      m_Type:',
                              '        m_SerializableType: %s' % stype,
                              "      m_SerializableObject: '%s'" % (json.dumps(value) if not isinstance(value, (int, float)) else value),
                              '    m_Space: 2147483647']
                else:
                    lines += ['  m_MasterData:', '    m_Owner: {fileID: 0}', '    m_Value:', '      m_Type:',
                              '        m_SerializableType: ', '      m_SerializableObject: ', '    m_Space: 2147483647']
            elif k == 'm_Property':
                lines += ['  m_Property:', '    name: %s' % v[0], '    m_serializedType:', '      m_SerializableType: %s' % v[1]]
            elif isinstance(v, list):
                if not v:
                    lines.append('  %s: []' % k)
                else:
                    lines.append('  %s:' % k)
                    lines += ['  - %s' % (ref(x) if isinstance(x, int) else x) for x in v]
            elif isinstance(v, str) and v.startswith('\n'):
                lines.append('  %s:%s' % (k, v))
            else:
                lines.append('  %s: %s' % (k, v))
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def ctx(cls, label, data, flow_in, flow_out, extra=None, inputs=(), children=()):
    f = {'m_Parent': ref(GRAPH), 'm_Children': list(children), 'm_UIPosition': '{x: 0, y: 0}', 'm_InputSlots': list(inputs),
         'm_OutputSlots': [], 'm_Label': label, 'm_Data': ref(data),
         'm_InputFlowSlot': '\n  - link:%s' % ''.join('\n    - context: {fileID: %d}\n      slotIndex: 0' % c for c in flow_in) if flow_in else '\n  - link: []',
         'm_OutputFlowSlot': '\n  - link:%s' % ''.join('\n    - context: {fileID: %d}\n      slotIndex: 0' % c for c in flow_out) if flow_out else '\n  - link: []'}
    f.update(extra or {})
    return f


def block(cls, parent, settings, slots_):
    i = node(cls, {})
    sl = [s(i) for s in slots_]
    docs[[d[0] for d in docs].index(i)] = (i, cls, dict({'m_Parent': ref(parent), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}',
                                                         'm_InputSlots': sl, 'm_OutputSlots': [], 'm_Disabled': 0}, **settings))
    return i


def fslot(name, value, stype=FLOAT, guid='sFloat', linked=()):
    return lambda owner: slot(owner, name, stype, value, guid, linked=linked)


def main():
    global GRAPH
    spark = None
    meta = os.path.join(HERE, 'Assets', 'FX', 'Textures', 'spark.png.meta')
    with open(meta, encoding='utf-8') as f:
        for line in f:
            if line.startswith('guid:'):
                spark = line.split()[1]
    GRAPH = nid()
    ids = {k: nid() for k in ('spawn1', 'init1', 'update1', 'out1', 'gpu', 'init2', 'update2', 'out2',
                              'dSp', 'dP1', 'dP2', 'dEv')}
    # operators: exposed parameter EmberRate (default 12; the VfxHolder prefab overrides it), random size, age curve
    param = nid()
    p_out = slot(param, 'o', FLOAT, 12, 'sFloat', direction=1)
    docs.append((param, 'param', {'m_Parent': ref(GRAPH), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}', 'm_InputSlots': [],
                                  'm_OutputSlots': [p_out], 'm_ExposedName': 'EmberRate', 'm_Exposed': 1, 'm_Order': 0,
                                  'm_Category': '', 'm_IsOutput': 0, 'm_ValueFilter': 0}))
    rnd = nid()
    r_min = slot(rnd, 'Min', FLOAT, 0.1, 'sFloat')
    r_max = slot(rnd, 'Max', FLOAT, 0.25, 'sFloat')
    r_out = slot(rnd, 'r', FLOAT, 0, 'sFloat', direction=1)
    docs.append((rnd, 'random', {'m_Parent': ref(GRAPH), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}', 'm_InputSlots': [r_min, r_max],
                                 'm_OutputSlots': [r_out], 'seed': 0, 'constant': 1, 'independentSeed': 0}))
    age = nid()
    a_out = slot(age, 't', FLOAT, 0, 'sFloat', direction=1)
    docs.append((age, 'age', {'m_Parent': ref(GRAPH), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}', 'm_InputSlots': [],
                              'm_OutputSlots': [a_out]}))
    sc = nid()
    curve = {'frames': [{'time': 0, 'value': 0.5, 'inTangent': 0, 'outTangent': 0}, {'time': 0.3, 'value': 1, 'inTangent': 0, 'outTangent': 0},
                        {'time': 1, 'value': 0, 'inTangent': 0, 'outTangent': 0}], 'preWrapMode': 8, 'postWrapMode': 8, 'version': 1}
    sc_curve = slot(sc, 'curve', CURVE, curve, 'sCurve')
    sc_time = slot(sc, 'time', FLOAT, 0, 'sFloat', linked=[a_out])
    sc_out = slot(sc, 's', FLOAT, 0, 'sFloat', direction=1)
    docs.append((sc, 'sampleCurve', {'m_Parent': ref(GRAPH), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}',
                                     'm_InputSlots': [sc_curve, sc_time], 'm_OutputSlots': [sc_out]}))

    # system 1: embers
    rate = block('rate', ids['spawn1'], {}, [fslot('Rate', 10, linked=[p_out])])
    burst = block('burst', ids['spawn1'], {'repeat': 0, 'spawnMode': 0, 'delayMode': 0}, [fslot('Count', 5), fslot('Delay', 0)])
    docs.append((ids['spawn1'], 'spawner', ctx('spawner', 'Embers', ids['dSp'], [], [ids['init1']],
                                               {'loopDuration': 0, 'loopCount': 0, 'delayBeforeLoop': 0, 'delayAfterLoop': 0},
                                               children=[rate, burst])))
    life = block('set', ids['init1'], {'attribute': 'lifetime', 'Composition': 0, 'Source': 0, 'Random': 2, 'channels': 6},
                 [fslot('A', 0.8), fslot('B', 1.4)])
    size = block('set', ids['init1'], {'attribute': 'size', 'Composition': 0, 'Source': 0, 'Random': 0, 'channels': 6},
                 [fslot('_Size', 0.1, linked=[r_out])])
    color = block('set', ids['init1'], {'attribute': 'color', 'Composition': 0, 'Source': 0, 'Random': 0, 'channels': 6},
                  [fslot('_Color', {'x': 1.0, 'y': 0.5, 'z': 0.125}, VEC3, 'sVec3')])
    shape = block('shape', ids['init1'], {'compositionPosition': 0, 'compositionAxes': 0, 'compositionDirection': 0, 'positionMode': 0,
                                          'spawnMode': 0, 'shape': 0, 'heightMode': 1, 'applyOrientation': 1, 'killOutliers': 0},
                  [fslot('arcSphere', {'sphere': {'transform': {'position': {'x': 0, 'y': 0.5, 'z': 0}, 'angles': {'x': 0, 'y': 0, 'z': 0},
                                                                'scale': {'x': 1, 'y': 1, 'z': 1}}, 'radius': 0.25}, 'arc': 6.2831855},
                         SPHERE, 'sStruct')])
    vel = block('veldir', ids['init1'], {'composition': 0, 'speedMode': 1},
                [fslot('center', {'position': {'x': 0, 'y': 0, 'z': 0}}, VEC3, 'sVec3'), fslot('DirectionBlend', 1),
                 fslot('MinSpeed', 1), fslot('MaxSpeed', 2.5)])
    docs.append((ids['init1'], 'init', ctx('init', '', ids['dP1'], [ids['spawn1']], [ids['update1']], children=[life, size, color, shape, vel])))
    grav = block('gravity', ids['update1'], {}, [fslot('Force', {'vector': {'x': 0, 'y': -2, 'z': 0}}, VECTOR, 'sVector')])
    turb = block('turb', ids['update1'], {'Mode': 1, 'NoiseType': 0},
                 [fslot('Intensity', 0.8), fslot('Drag', 0.5), fslot('frequency', 2), fslot('octaves', 2, UINT, 'sUint')])
    trig = nid()
    evt_out = nid()
    gpu_in = slot(ids['gpu'], 'evt', GPUEVT, {}, 'sUndefined', linked=[evt_out])
    docs.append((evt_out, 'sUndefined', {'m_Parent': ref(0), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}', 'm_MasterSlot': ref(evt_out),
                                         '_master': (trig, GPUEVT, {}), 'm_Property': ('evt', GPUEVT), 'm_Direction': 1,
                                         'm_LinkedSlots': [gpu_in]}))
    t_count = slot(trig, 'count', UINT, 1, 'sUint')
    docs.append((trig, 'trigger', {'m_Parent': ref(ids['update1']), 'm_Children': [], 'm_UIPosition': '{x: 0, y: 0}',
                                   'm_InputSlots': [t_count], 'm_OutputSlots': [evt_out], 'm_Disabled': 0, 'mode': 1, 'clampToOne': 0}))
    docs.append((ids['update1'], 'update', ctx('update', '', ids['dP1'], [ids['init1']], [ids['out1']],
                                               {'integration': 0, 'angularIntegration': 0, 'ageParticles': 1, 'reapParticles': 1,
                                                'skipZeroDeltaUpdate': 0}, children=[grav, turb, trig])))
    orient = block('orient', ids['out1'], {'mode': 6, 'axes': 4, 'faceRay': 1}, [])
    grad = {'colorKeys': [{'color': {'r': 1, 'g': 1, 'b': 1, 'a': 1}, 'time': 0}, {'color': {'r': 1, 'g': 0.25, 'b': 0, 'a': 1}, 'time': 1}],
            'alphaKeys': [{'alpha': 0, 'time': 0}, {'alpha': 1, 'time': 0.1}, {'alpha': 0, 'time': 1}], 'gradientMode': 0}
    colc = block('curve', ids['out1'], {'attribute': 'color', 'Composition': 2, 'AlphaComposition': 2, 'SampleMode': 0, 'Mode': 1,
                                        'ColorMode': 3, 'channels': 6}, [fslot('Color', grad, GRAD, 'sGrad')])
    sizel = block('set', ids['out1'], {'attribute': 'size', 'Composition': 2, 'Source': 0, 'Random': 0, 'channels': 6},
                  [fslot('_Size', 1, linked=[sc_out])])
    tex = slot(ids['out1'], 'mainTexture', TEX, {'obj': {'fileID': 2800000, 'guid': spark, 'type': 3}}, 'sTex')
    docs.append((ids['out1'], 'quad', ctx('quad', 'Embers', ids['dP1'], [ids['update1']], [],
                                          {'blendMode': 0, 'cullMode': 0, 'zWriteMode': 0, 'zTestMode': 0, 'useAlphaClipping': 0,
                                           'sortingPriority': 0, 'uvMode': 0, 'useSoftParticle': 1, 'sort': 0, 'primitiveType': 1},
                                          inputs=[tex], children=[orient, colc, sizel])))
    # system 2: sparks started by the GPU event
    docs.append((ids['gpu'], 'gpuevent', ctx('gpuevent', '', ids['dEv'], [], [ids['init2']], inputs=[gpu_in])))
    life2 = block('set', ids['init2'], {'attribute': 'lifetime', 'Composition': 0, 'Source': 0, 'Random': 0, 'channels': 6},
                  [fslot('_Lifetime', 0.4)])
    size2 = block('set', ids['init2'], {'attribute': 'size', 'Composition': 0, 'Source': 0, 'Random': 0, 'channels': 6},
                  [fslot('_Size', 0.05)])
    vel2 = block('set', ids['init2'], {'attribute': 'velocity', 'Composition': 0, 'Source': 0, 'Random': 2, 'channels': 6},
                 [fslot('A', {'x': -1, 'y': 1, 'z': -1}, VEC3, 'sVec3'), fslot('B', {'x': 1, 'y': 2, 'z': 1}, VEC3, 'sVec3')])
    pos2 = block('set', ids['init2'], {'attribute': 'position', 'Composition': 0, 'Source': 1, 'Random': 0, 'channels': 6}, [])
    docs.append((ids['init2'], 'init', ctx('init', 'Sparks', ids['dP2'], [ids['gpu']], [ids['update2']], children=[life2, size2, vel2, pos2])))
    docs.append((ids['update2'], 'update', ctx('update', '', ids['dP2'], [ids['init2']], [ids['out2']],
                                               {'integration': 0, 'angularIntegration': 0, 'ageParticles': 1, 'reapParticles': 1,
                                                'skipZeroDeltaUpdate': 0})))
    docs.append((ids['out2'], 'quad', ctx('quad', 'Sparks', ids['dP2'], [ids['update2']], [],
                                          {'blendMode': 0, 'uvMode': 0, 'useSoftParticle': 0, 'primitiveType': 1})))
    # data
    for k, cap in (('dP1', 128), ('dP2', 64)):
        docs.append((ids[k], 'dataParticle', {'title': '', 'dataType': 0, 'capacity': cap, 'stripCapacity': 16,
                                              'particlePerStripCount': 16, 'm_Space': 1}))
    docs.append((ids['dSp'], 'dataSpawner', {'title': ''}))
    docs.append((ids['dEv'], 'dataSpawner', {'title': ''}))
    top = [ids[k] for k in ('spawn1', 'init1', 'update1', 'out1', 'gpu', 'init2', 'update2', 'out2')] + [param, rnd, age, sc]
    docs.insert(0, (GRAPH, 'graph', {'m_Parent': ref(0), 'm_Children': top, 'm_UIPosition': '{x: 0, y: 0}', 'm_GraphVersion': 18}))
    write()
    print('wrote', OUT)


if __name__ == '__main__':
    main()
