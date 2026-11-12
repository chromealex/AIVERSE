#!/usr/bin/env python3
"""Tests of the Unity animation importer (stdlib unittest), on synthetic Unity YAML written here.

    python3 tools/importers/unity_anim/test_unity_anim.py
    AIVERSE_CLIPINFO=build/<cfg>/aiverse_clipinfo python3 tools/importers/unity_anim/test_unity_anim.py   # + engine dry run
"""
import json
import math
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import anim as A  # noqa: E402
import controller as CT  # noqa: E402
import importer as IM  # noqa: E402
from lib.unity import unity_yaml as Y  # noqa: E402

G = {name: '%032x' % (0xA000 + i) for i, name in enumerate(
    ['bot', 'walk', 'run', 'idle', 'shoot', 'spin', 'ui', 'mask', 'ctrl', 'over', 'prefab', 'left', 'right'])}
HEAD = '%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n'


def key3(t, v, s=(0, 0, 0), wm=0):
    vec = lambda a: '{x: %g, y: %g, z: %g}' % tuple(a)
    return ('      - serializedVersion: 3\n        time: %g\n        value: %s\n        inSlope: %s\n        outSlope: %s\n'
            '        tangentMode: 0\n        weightedMode: %d\n        inWeight: {x: 0.5, y: 0.5, z: 0.5}\n        outWeight: {x: 0.5, y: 0.5, z: 0.5}\n'
            % (t, vec(v), vec(s), vec(s), wm))


def key4(t, v, s=(0, 0, 0, 0), wm=0):
    vec = lambda a: '{x: %g, y: %g, z: %g, w: %g}' % tuple(a)
    return ('      - serializedVersion: 3\n        time: %g\n        value: %s\n        inSlope: %s\n        outSlope: %s\n'
            '        tangentMode: 0\n        weightedMode: %d\n        inWeight: {x: 0.2, y: 0.2, z: 0.2, w: 0.2}\n        outWeight: {x: 0.2, y: 0.2, z: 0.2, w: 0.2}\n'
            % (t, vec(v), vec(s), vec(s), wm))


def curve(keys, path, order=None):
    s = '  - curve:\n      serializedVersion: 2\n      m_Curve:\n' + ''.join(keys) + '      m_PreInfinity: 2\n      m_PostInfinity: 2\n'
    if order is not None:
        s += '      m_RotationOrder: %d\n' % order
    return s + '    path: %s\n' % path


def clip(name, rot='', pos='', euler='', floats='', loop=1, stop=1):
    def sec(n, body):
        return '  %s:%s\n%s' % (n, '' if body else ' []', body)
    return (HEAD + '--- !u!74 &7400000\nAnimationClip:\n  m_ObjectHideFlags: 0\n  m_Name: %s\n  serializedVersion: 7\n  m_Legacy: 0\n'
            '  m_Compressed: 0\n  m_UseHighQualityCurve: 1\n' % name + sec('m_RotationCurves', rot) + '  m_CompressedRotationCurves: []\n' +
            sec('m_EulerCurves', euler) + sec('m_PositionCurves', pos) + '  m_ScaleCurves: []\n' + sec('m_FloatCurves', floats) +
            '  m_PPtrCurves: []\n  m_SampleRate: 30\n  m_WrapMode: 0\n  m_AnimationClipSettings:\n    serializedVersion: 2\n'
            '    m_StartTime: 0\n    m_StopTime: %g\n    m_LoopTime: %d\n    m_LoopBlendOrientation: 0\n    m_LoopBlendPositionY: 1\n'
            '    m_LoopBlendPositionXZ: 0\n    m_Mirror: 0\n  m_EditorCurves:\n  - curve:\n      serializedVersion: 2\n      m_Curve: []\n'
            '    attribute: m_LocalPosition.x\n    path: junk\n  m_EulerEditorCurves: []\n  m_HasGenericRootTransform: 0\n  m_Events: []\n' % (stop, loop))


def meta(guid, body=''):
    return 'fileFormatVersion: 2\nguid: %s\n%s' % (guid, body)


def ref(name, fid=7400000):
    return '{fileID: %d, guid: %s, type: 2}' % (fid, G[name])


CONTROLLER = HEAD + '''--- !u!91 &9100000
AnimatorController:
  m_Name: Bot
  serializedVersion: 5
  m_AnimatorParameters:
  - m_Name: Speed
    m_Type: 1
    m_DefaultFloat: 0
  - m_Name: MoveX
    m_Type: 1
    m_DefaultFloat: 0
  - m_Name: MoveZ
    m_Type: 1
    m_DefaultFloat: 0
  - m_Name: Progress
    m_Type: 1
    m_DefaultFloat: 0
  - m_Name: Shoot
    m_Type: 9
    m_DefaultBool: 0
  - m_Name: Armed
    m_Type: 4
    m_DefaultBool: 1
  m_AnimatorLayers:
  - serializedVersion: 5
    m_Name: Base Layer
    m_StateMachine: {fileID: 1100}
    m_Mask: {fileID: 0}
    m_BlendingMode: 0
    m_SyncedLayerIndex: -1
    m_DefaultWeight: 0
    m_IKPass: 0
  - serializedVersion: 5
    m_Name: Upper
    m_StateMachine: {fileID: 1200}
    m_Mask: {fileID: 31900000, guid: MASKGUID, type: 2}
    m_BlendingMode: 0
    m_SyncedLayerIndex: -1
    m_DefaultWeight: 0.75
    m_IKPass: 1
--- !u!1107 &1100
AnimatorStateMachine:
  m_Name: Base Layer
  m_ChildStates:
  - serializedVersion: 1
    m_State: {fileID: -5206838344684310375}
  m_ChildStateMachines:
  - serializedVersion: 1
    m_StateMachine: {fileID: 1300}
  m_AnyStateTransitions:
  - {fileID: 2003}
  m_EntryTransitions: []
  m_StateMachineTransitions: {}
  m_StateMachineBehaviours: []
  m_DefaultState: {fileID: -5206838344684310375}
--- !u!1107 &1300
AnimatorStateMachine:
  m_Name: Combat
  m_ChildStates:
  - serializedVersion: 1
    m_State: {fileID: 1003}
  m_ChildStateMachines: []
  m_AnyStateTransitions: []
  m_EntryTransitions: []
  m_StateMachineTransitions: {}
  m_StateMachineBehaviours: []
  m_DefaultState: {fileID: 1003}
--- !u!1102 &-5206838344684310375
AnimatorState:
  m_Name: Move
  m_Speed: 1
  m_CycleOffset: 0
  m_Transitions: []
  m_StateMachineBehaviours: []
  m_IKOnFeet: 0
  m_Mirror: 0
  m_SpeedParameterActive: 1
  m_TimeParameterActive: 0
  m_Motion: {fileID: 2060}
  m_Tag:
  m_SpeedParameter: Speed
  m_TimeParameter:
--- !u!206 &2060
BlendTree:
  m_Name: Locomotion
  m_Childs:
  - serializedVersion: 2
    m_Motion: IDLEREF
    m_Threshold: 0
    m_Position: {x: 0, y: 0}
    m_TimeScale: 1
    m_CycleOffset: 0
    m_DirectBlendParameter: Speed
    m_Mirror: 0
  - serializedVersion: 2
    m_Motion: {fileID: 2061}
    m_Threshold: 1
    m_Position: {x: 0, y: 0}
    m_TimeScale: 1
    m_CycleOffset: 0
    m_DirectBlendParameter: Speed
    m_Mirror: 0
  m_BlendParameter: Speed
  m_BlendParameterY: Speed
  m_BlendType: 0
--- !u!206 &2061
BlendTree:
  m_Name: Directions
  m_Childs:
  - serializedVersion: 2
    m_Motion: WALKREF
    m_Threshold: 0
    m_Position: {x: 0, y: 1}
    m_TimeScale: 1
  - serializedVersion: 2
    m_Motion: LEFTREF
    m_Threshold: 0
    m_Position: {x: -1, y: 0}
    m_TimeScale: 1
  - serializedVersion: 2
    m_Motion: RIGHTREF
    m_Threshold: 0
    m_Position: {x: 1, y: 0}
    m_TimeScale: 2
  m_BlendParameter: MoveX
  m_BlendParameterY: MoveZ
  m_BlendType: 2
--- !u!1102 &1003
AnimatorState:
  m_Name: Swing
  m_Speed: 1
  m_Transitions:
  - {fileID: 2004}
  m_StateMachineBehaviours: []
  m_SpeedParameterActive: 0
  m_TimeParameterActive: 1
  m_Motion: SHOOTREF
  m_TimeParameter: Progress
--- !u!1101 &2003
AnimatorStateTransition:
  m_Conditions:
  - m_ConditionMode: 3
    m_ConditionEvent: Progress
    m_EventTreshold: 0.001
  m_DstStateMachine: {fileID: 0}
  m_DstState: {fileID: 1003}
  m_Solo: 0
  m_Mute: 0
  m_IsExit: 0
  m_TransitionDuration: 0.1
  m_TransitionOffset: 0
  m_ExitTime: 0.9
  m_HasExitTime: 0
  m_HasFixedDuration: 1
  m_InterruptionSource: 2
  m_OrderedInterruption: 1
  m_CanTransitionToSelf: 0
--- !u!1101 &2004
AnimatorStateTransition:
  m_Conditions:
  - m_ConditionMode: 4
    m_ConditionEvent: Progress
    m_EventTreshold: 0.001
  m_DstStateMachine: {fileID: 0}
  m_DstState: {fileID: 0}
  m_Solo: 0
  m_Mute: 0
  m_IsExit: 1
  m_TransitionDuration: 0.25
  m_TransitionOffset: 0
  m_ExitTime: 0.75
  m_HasExitTime: 1
  m_HasFixedDuration: 0
  m_InterruptionSource: 0
  m_OrderedInterruption: 1
  m_CanTransitionToSelf: 1
--- !u!1101 &2005
AnimatorStateTransition:
  m_Conditions:
  - m_ConditionMode: 1
    m_ConditionEvent: Shoot
    m_EventTreshold: 0
  m_DstStateMachine: {fileID: 0}
  m_DstState: {fileID: 1202}
  m_Solo: 0
  m_Mute: 0
  m_IsExit: 0
  m_TransitionDuration: 0
  m_HasExitTime: 0
  m_HasFixedDuration: 1
  m_InterruptionSource: 0
  m_CanTransitionToSelf: 1
--- !u!1101 &2006
AnimatorStateTransition:
  m_Conditions:
  - m_ConditionMode: 2
    m_ConditionEvent: Armed
    m_EventTreshold: 0
  m_DstStateMachine: {fileID: 0}
  m_DstState: {fileID: 1202}
  m_Solo: 0
  m_Mute: 1
  m_IsExit: 0
  m_TransitionDuration: 0
  m_HasExitTime: 0
  m_HasFixedDuration: 1
--- !u!1107 &1200
AnimatorStateMachine:
  m_Name: Upper
  m_ChildStates:
  - serializedVersion: 1
    m_State: {fileID: 1201}
  - serializedVersion: 1
    m_State: {fileID: 1202}
  m_ChildStateMachines: []
  m_AnyStateTransitions: []
  m_EntryTransitions: []
  m_StateMachineTransitions: {}
  m_StateMachineBehaviours: []
  m_DefaultState: {fileID: 1201}
--- !u!1102 &1201
AnimatorState:
  m_Name: None
  m_Speed: 1
  m_Transitions:
  - {fileID: 2005}
  - {fileID: 2006}
  m_StateMachineBehaviours: []
  m_Motion: {fileID: 0}
--- !u!1102 &1202
AnimatorState:
  m_Name: Fire
  m_Speed: 2
  m_Transitions: []
  m_StateMachineBehaviours: []
  m_Motion: SHOOTREF
'''

MASK = HEAD + '''--- !u!319 &31900000
AvatarMask:
  m_Name: UpperBody
  m_Mask: 01000000
  m_Elements:
  - m_Path:
    m_Weight: 1
  - m_Path: Root
    m_Weight: 0
  - m_Path: Root/Hips
    m_Weight: 0
  - m_Path: Root/Hips/Spine
    m_Weight: 1
  - m_Path: Root/Hips/Spine/Arm
    m_Weight: 1
'''

OVERRIDE = HEAD + '''--- !u!221 &22100000
AnimatorOverrideController:
  m_Name: BotRun
  m_Controller: {fileID: 9100000, guid: CTRLGUID, type: 2}
  m_Clips:
  - m_OriginalClip: WALKREF
    m_OverrideClip: RUNREF
'''

PREFAB = HEAD + '''--- !u!1 &100
GameObject:
  m_Name: Bot
--- !u!95 &200
Animator:
  m_GameObject: {fileID: 100}
  m_Avatar: {fileID: 9000000, guid: BOTGUID, type: 3}
  m_Controller: {fileID: 22100000, guid: OVERGUID, type: 2}
  m_ApplyRootMotion: 0
'''

GLTF = {
    'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
    'nodes': [{'name': 'Root', 'children': [1]}, {'name': 'Hips', 'children': [2, 4], 'translation': [0, 1, 0]},
              {'name': 'Spine', 'children': [3], 'translation': [0, 0.5, 0]}, {'name': 'Arm', 'translation': [-0.3, 0.2, 0]},
              {'name': 'Leg', 'translation': [0.1, -0.5, 0]}],
}


def walk_pos(dz):
    return curve([key3(0, (0.1, 1, 0), (0, 0, dz)), key3(1, (0.1, 1, dz), (0, 0, dz))], 'Root/Hips')


def write_project(root):
    a = os.path.join(root, 'Assets', 'Bot')
    os.makedirs(os.path.join(a, 'Anims'))
    os.makedirs(os.path.join(root, 'Assets', 'Scripts'))
    files = {
        'Bot.gltf': (json.dumps(GLTF), meta(G['bot'], 'ModelImporter:\n  animations:\n    motionNodeName: Root/Hips\n    clipAnimations: []\n  animationType: 2\n')),
        'Anims/Walk.anim': (clip('Walk', pos=walk_pos(1)), meta(G['walk'])),
        'Anims/Run.anim': (clip('Run', pos=walk_pos(3), stop=0.5), meta(G['run'])),
        'Anims/Idle.anim': (clip('Idle', rot=curve([key4(0, (0, 0, 0, 1)), key4(2, (0, 0.2588, 0, 0.9659))], 'Root/Hips/Spine'), stop=2), meta(G['idle'])),
        'Anims/Left.anim': (clip('Left', pos=curve([key3(0, (-1, 1, 0)), key3(1, (-1, 1, 0))], 'Root/Hips')), meta(G['left'])),
        'Anims/Right.anim': (clip('Right', pos=curve([key3(0, (1, 1, 0)), key3(1, (1, 1, 0))], 'Root/Hips')), meta(G['right'])),
        'Anims/Shoot.anim': (clip('Shoot', loop=0, rot=curve([key4(0, (0, 0, 0, 1), wm=3), key4(1, (0.7071, 0, 0, 0.7071), wm=3)], 'Root/Hips/Spine/Arm')), meta(G['shoot'])),
        'Anims/Spin.anim': (clip('Spin', euler=curve([key3(0, (0, 0, 0)), key3(1, (0, 90, 0), (0, 90, 0))], 'Root', order=4)), meta(G['spin'])),
        'Anims/UiFade.anim': (clip('UiFade', floats='  - serializedVersion: 2\n    curve:\n      serializedVersion: 2\n      m_Curve: []\n    attribute: m_Alpha\n    path: Panel\n    classID: 225\n'), meta(G['ui'])),
        'UpperBody.mask': (MASK, meta(G['mask'])),
        'Bot.controller': (CONTROLLER.replace('MASKGUID', G['mask']).replace('IDLEREF', ref('idle')).replace('WALKREF', ref('walk'))
                           .replace('LEFTREF', ref('left')).replace('RIGHTREF', ref('right')).replace('SHOOTREF', ref('shoot')), meta(G['ctrl'])),
        'BotRun.overrideController': (OVERRIDE.replace('CTRLGUID', G['ctrl']).replace('WALKREF', ref('walk')).replace('RUNREF', ref('run')), meta(G['over'])),
        'Bot.prefab': (PREFAB.replace('BOTGUID', G['bot']).replace('OVERGUID', G['over']), meta(G['prefab'])),
    }
    for rel, (body, m) in files.items():
        with open(os.path.join(a, rel), 'w') as f:
            f.write(body)
        with open(os.path.join(a, rel) + '.meta', 'w') as f:
            f.write(m)
    with open(os.path.join(root, 'Assets', 'Scripts', 'BotView.cs'), 'w') as f:
        f.write('using UnityEngine;\nclass BotView { static readonly int s = Animator.StringToHash("Speed");\n'
                ' void Go(Animator a) { a.SetTrigger("Shoot"); a.SetLayerWeight(a.GetLayerIndex("Upper"), 1); } }\n')
    return a


def load_json(path):
    with open(path) as f:
        return json.load(f)


def read_glb(data):
    magic, version, total = struct.unpack_from('<III', data, 0)
    n, kind = struct.unpack_from('<II', data, 12)
    doc = json.loads(data[20:20 + n])
    bn = struct.unpack_from('<I', data, 20 + n)[0]
    return doc, data[28 + n:28 + n + bn], magic, total


def floats(doc, binary, acc):
    a = doc['accessors'][acc]
    v = doc['bufferViews'][a['bufferView']]
    return struct.unpack_from('<%df' % (v['byteLength'] // 4), binary, v['byteOffset'])


class UnityAnimTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='unity_anim_')
        cls.assets = write_project(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def path(self, rel):
        return os.path.join(self.assets, rel)

    def test_clip_cubic_and_mirror(self):
        c = A.load(self.path('Anims/Walk.anim'))
        self.assertTrue(c['loop'])
        self.assertEqual(A.kind_of(c), 'transform')
        data, info = A.to_glb(c)
        self.assertEqual((info['cubic'], info['resampled']), (1, 0))
        doc, binary, magic, total = read_glb(data)
        self.assertEqual((magic, total), (0x46546C67, len(data)))
        self.assertEqual([n['name'] for n in doc['nodes']], ['Root', 'Hips'])
        s = doc['animations'][0]['samplers'][0]
        self.assertEqual(s['interpolation'], 'CUBICSPLINE')
        v = floats(doc, binary, s['output'])
        # key 0: in-tangent, value, out-tangent; x is mirrored (Unity is left-handed)
        self.assertAlmostEqual(v[3], -0.1, 5)
        self.assertAlmostEqual(v[8], 1.0, 5)   # out-tangent z (slope per second)

    def test_weighted_resampled_and_euler(self):
        c = A.load(self.path('Anims/Shoot.anim'))
        self.assertFalse(c['loop'])
        data, info = A.to_glb(c)
        self.assertEqual(info['resampled'], 1)
        doc, binary, *_ = read_glb(data)
        s = doc['animations'][0]['samplers'][0]
        self.assertEqual(s['interpolation'], 'LINEAR')
        t = floats(doc, binary, s['input'])
        self.assertEqual(len(t), 31)
        q = floats(doc, binary, s['output'])
        self.assertAlmostEqual(q[-4], 0.7071, 3)   # x kept, y/z mirrored
        spin = A.load(self.path('Anims/Spin.anim'))
        data, info = A.to_glb(spin)
        doc, binary, *_ = read_glb(data)
        q = floats(doc, binary, doc['animations'][0]['samplers'][0]['output'])
        self.assertAlmostEqual(q[-3], -math.sin(math.radians(45)), 3)   # 90 deg about Y, mirrored
        self.assertAlmostEqual(q[-1], math.cos(math.radians(45)), 3)

    def test_ui_clip_detected(self):
        c = A.load(self.path('Anims/UiFade.anim'))
        self.assertEqual(A.kind_of(c), 'ui')
        self.assertTrue(any('CanvasGroup' in u for u in A.unsupported(c)))

    def test_controller_conversion(self):
        idx = IM.Index(os.path.join(self.tmp, 'Assets'))
        names = {}

        def resolve(key):
            p = idx.path(key[0])
            names[key] = os.path.splitext(os.path.basename(p))[0]
            return {'name': names[key], 'loop': names[key] != 'Shoot'}
        mask = lambda r: CT.load_mask(idx.docs(idx.path(r.get('guid'))))
        conv = CT.Converter(self.path('Bot.controller'), idx.docs(self.path('Bot.controller')), G['ctrl'], resolve, mask)
        graph, types = conv.convert()
        ctl = graph['nodes'][0]['controller']
        self.assertEqual(ctl['triggers'], ['Shoot'])
        self.assertEqual(types['Armed'], 'bool')
        self.assertEqual(graph['parameters']['Armed'], 1)
        self.assertEqual(graph['parameters']['layer.Upper'], 0.75)
        base, upper = ctl['layers']
        self.assertEqual([s['name'] for s in base['states']], ['Move', 'Combat/Swing'])
        move = base['states'][0]
        self.assertEqual(move['speedParameter'], 'Speed')
        tree = move['motion']['tree']
        self.assertEqual((tree['type'], tree['parameter']), ('1d', 'Speed'))
        inner = tree['children'][1]['motion']['tree']
        self.assertEqual((inner['type'], inner['parameterY']), ('freeform2d', 'MoveZ'))
        self.assertEqual(inner['children'][2]['timeScale'], 2)
        swing = base['states'][1]
        self.assertEqual(swing['timeParameter'], 'Progress')
        self.assertEqual(swing['transitions'][0], {'exit': True, 'conditions': [{'parameter': 'Progress', 'mode': 'less', 'threshold': 0.001}],
                                                   'hasExitTime': True, 'exitTime': 0.75, 'duration': 0.25, 'fixedDuration': False})
        anyt = base['anyState'][0]
        self.assertEqual((anyt['to'], anyt['interruption'], anyt['canTransitionToSelf']), ('Combat/Swing', 'destination', False))
        self.assertEqual(upper['mask'], {'Root': 0, 'Root/Hips': 0, 'Root/Hips/Spine': 1, 'Root/Hips/Spine/Arm': 1})
        self.assertIsNone(upper['states'][0]['motion'])
        self.assertEqual(len(upper['states'][0]['transitions']), 1)   # the muted one is dropped
        self.assertEqual(upper['states'][1]['speed'], 2)
        self.assertTrue(any('IK pass' in n for n in conv.notes))

    def test_import_project(self):
        out = os.path.join(self.tmp, 'out')
        clipinfo = os.environ.get('AIVERSE_CLIPINFO')
        rep = IM.Importer(self.assets, out, clipinfo=clipinfo, scripts=[os.path.join(self.tmp, 'Assets', 'Scripts')]).run()
        self.assertEqual(len(rep['models']), 1)
        side = load_json(os.path.join(out, 'models', 'Bot', 'Bot.gltf.clips.json'))
        by = {c['name']: c for c in side['clips']}
        self.assertIn('Run', by)
        self.assertNotIn('Walk', by)   # the override replaced it everywhere
        self.assertEqual(by['Run']['file'], '../../animations/clips/Run.glb')
        self.assertEqual(by['Run']['rootMotion'], {'node': 'Root/Hips', 'bakeRotation': False, 'bakeY': True, 'bakeXZ': False})
        self.assertTrue(side['keepSource'])
        c = next(c for c in rep['controllers'] if c['controller'].endswith('BotRun.overrideController'))
        self.assertEqual(c['prefabs'], ['Bot/Bot.prefab'])
        self.assertEqual(c['parameters']['Speed']['drivers'], ['BotView.cs'])
        self.assertEqual(c['parameters']['layer.Upper']['drivers'], ['BotView.cs'])
        self.assertEqual(c['crowd']['idle'], 'Idle')
        graph = load_json(os.path.join(out, c['graph']))
        self.assertEqual(graph['model'], 'models/Bot/Bot.gltf')
        self.assertTrue(os.path.exists(os.path.join(out, 'unity_anim_report.md')))
        if clipinfo:
            m = rep['models'][0]['engine']
            self.assertTrue(m['ok'], m)
            self.assertIn('Run', m['clips'])
            self.assertEqual(m['warnings'], [])
            self.assertTrue(c['engine']['ok'], c['engine'])

    def test_output_inside_source_rejected(self):
        with self.assertRaises(ValueError):
            IM.Importer(self.assets, os.path.join(self.assets, 'out'))


if __name__ == '__main__':
    unittest.main()
