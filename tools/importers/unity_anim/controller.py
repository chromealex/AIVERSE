"""Unity AnimatorController / AnimatorOverrideController / AvatarMask -> AIVERSE animation graph
with one {kind: "controller"} node (engine/render/AnimationController.h).

`resolve(ref)` maps a Unity motion reference ({fileID, guid}) to an engine clip name (or None); the
importer owns clip naming and conversion. Everything without an engine equivalent is listed in
`notes` (state machine behaviours, IK pass, mirror, conditional entry transitions, ...)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from lib.unity import unity_yaml as Y  # noqa: E402

C_CONTROLLER, C_OVERRIDE, C_STATE, C_MACHINE, C_TRANSITION, C_ENTRY, C_TREE, C_MASK = 91, 221, 1102, 1107, 1101, 1109, 206, 319
PARAM_TYPES = {1: 'float', 3: 'int', 4: 'bool', 9: 'trigger'}
MODES = {1: 'if', 2: 'ifNot', 3: 'greater', 4: 'less', 6: 'equals', 7: 'notEqual'}
INTERRUPT = {0: 'none', 1: 'source', 2: 'destination', 3: 'sourceThenDestination', 4: 'destinationThenSource'}
TREES = {0: '1d', 1: 'simple2d', 2: 'freeform2d', 3: 'cartesian2d', 4: 'direct'}


def fid(v):
    """A fileID exactly (19-digit ids do not survive a float)."""
    if isinstance(v, int):
        return v
    try:
        return int(str(v).strip())
    except (TypeError, ValueError):
        return 0


def ref_key(ref, owner_guid):
    if not isinstance(ref, dict):
        return None
    fid_ = fid(ref.get('fileID'))
    if fid_ == 0:
        return None
    return ((ref.get('guid') or owner_guid or '').lower(), fid_)


def load_mask(docs):
    """AvatarMask transform paths -> {path: weight} (None when it only has humanoid body parts)."""
    for d in docs:
        if d.class_id == C_MASK:
            els = d.data.get('m_Elements') or []
            out = {e.get('m_Path'): Y.num(e.get('m_Weight'), 0) for e in els if e.get('m_Path')}
            return out or None, d.data.get('m_Name') or ''
    return None, ''


class Converter:
    def __init__(self, path, docs, guid, resolve, load_mask_ref, overrides=None, layer_param_prefix='layer.'):
        self.path, self.guid, self.resolve, self.load_mask_ref = path, guid, resolve, load_mask_ref
        self.by_id = {d.file_id: d for d in docs}
        self.docs = docs
        self.overrides = overrides or {}
        self.prefix = layer_param_prefix
        self.notes, self.clips = [], []
        self.where = os.path.basename(path)

    def note(self, s):
        if s not in self.notes:
            self.notes.append(s)

    # ---------------------------------------------------------------- motions
    def motion(self, ref, depth=0):
        key = ref_key(ref, self.guid)
        if key is None:
            return None
        key = self.overrides.get(key, key)
        if key[0] == self.guid and key[1] in self.by_id and self.by_id[key[1]].class_id == C_TREE:
            if depth > 8:
                self.note('blend tree nesting deeper than 8: cut')
                return None
            return self.tree(self.by_id[key[1]].data, depth)
        clip = self.resolve(key)
        if clip is None:
            self.note('motion %s/%s not resolved (missing or unsupported clip)' % key)
            return None
        self.clips.append(clip['name'])
        return {'clip': clip['name'], 'loop': clip.get('loop', True)}

    def tree(self, t, depth):
        kind = TREES.get(int(Y.num(t.get('m_BlendType'), 0)), '1d')
        if kind == 'simple2d':
            self.note('2D Simple Directional blend trees use the freeform directional weights')
        out = {'type': kind, 'parameter': t.get('m_BlendParameter') or '', 'children': []}
        if kind in ('simple2d', 'freeform2d', 'cartesian2d'):
            out['parameterY'] = t.get('m_BlendParameterY') or ''
        if kind == 'direct':
            out['normalize'] = bool(t.get('m_NormalizedBlendValues'))
        for c in t.get('m_Childs') or []:
            pos = c.get('m_Position') or {}
            ch = {'motion': self.motion(c.get('m_Motion'), depth + 1), 'threshold': Y.num(c.get('m_Threshold'), 0),
                  'position': [Y.num(pos.get('x'), 0), Y.num(pos.get('y'), 0)], 'timeScale': Y.num(c.get('m_TimeScale'), 1) or 1}
            if Y.num(c.get('m_CycleOffset'), 0):
                ch['cycleOffset'] = Y.num(c.get('m_CycleOffset'), 0)
            if kind == 'direct':
                ch['directParameter'] = c.get('m_DirectBlendParameter') or ''
            if c.get('m_Mirror'):
                self.note('mirrored blend tree child (mirror is not supported)')
            out['children'].append(ch)
        return {'tree': out}

    # ---------------------------------------------------------------- state machines
    def flatten(self, machine_id, prefix, states, any_state, entry_notes):
        """Collects states of a (sub) state machine; names of nested machines are prefixed."""
        m = self.by_id.get(machine_id)
        if m is None:
            return None
        d = m.data
        for cs in d.get('m_ChildStates') or []:
            sid = fid((cs.get('m_State') or {}).get('fileID'))
            if sid in self.by_id:
                states[sid] = prefix + (self.by_id[sid].data.get('m_Name') or 'State')
        for t in d.get('m_AnyStateTransitions') or []:
            any_state.append(fid(t.get('fileID')))
        if d.get('m_EntryTransitions'):
            entry_notes.append('conditional entry transitions of "%s" (the default state is used)' % (d.get('m_Name') or ''))
        if d.get('m_StateMachineTransitions'):
            self.note('state machine transitions (exits of sub-machines) go to the layer default')
        if d.get('m_StateMachineBehaviours'):
            self.note('state machine behaviours (scripts) on "%s"' % (d.get('m_Name') or ''))
        for sm in d.get('m_ChildStateMachines') or []:
            sub = fid((sm.get('m_StateMachine') or {}).get('fileID'))
            sd = self.by_id.get(sub)
            name = (sd.data.get('m_Name') if sd else 'Sub') or 'Sub'
            self.flatten(sub, prefix + name + '/', states, any_state, entry_notes)
        return fid((d.get('m_DefaultState') or {}).get('fileID'))

    def machine_default(self, mid):
        m = self.by_id.get(mid)
        return fid((m.data.get('m_DefaultState') or {}).get('fileID')) if m else 0

    def transition(self, tid, names, layer_default):
        t = self.by_id.get(tid)
        if t is None:
            return None
        d = t.data
        if d.get('m_Mute'):
            return None
        out = {}
        if d.get('m_IsExit'):
            out['exit'] = True
        else:
            dst = fid((d.get('m_DstState') or {}).get('fileID'))
            if not dst:
                dst = self.machine_default(fid((d.get('m_DstStateMachine') or {}).get('fileID')))
            if dst not in names:
                self.note('transition to an unknown state dropped')
                return None
            out['to'] = names[dst]
        conds = []
        for c in d.get('m_Conditions') or []:
            mode = MODES.get(int(Y.num(c.get('m_ConditionMode'), 1)))
            if mode is None:
                self.note('condition mode %s dropped' % c.get('m_ConditionMode'))
                continue
            conds.append({'parameter': c.get('m_ConditionEvent') or '', 'mode': mode, 'threshold': Y.num(c.get('m_EventTreshold'), 0)})
        out['conditions'] = conds
        if d.get('m_HasExitTime'):
            out['hasExitTime'] = True
            out['exitTime'] = Y.num(d.get('m_ExitTime'), 0)
        out['duration'] = Y.num(d.get('m_TransitionDuration'), 0)
        out['fixedDuration'] = bool(d.get('m_HasFixedDuration', 1))
        if Y.num(d.get('m_TransitionOffset'), 0):
            out['offset'] = Y.num(d.get('m_TransitionOffset'), 0)
        inter = INTERRUPT.get(int(Y.num(d.get('m_InterruptionSource'), 0)), 'none')
        if inter != 'none':
            out['interruption'] = inter
            if not d.get('m_OrderedInterruption', 1):
                self.note('unordered interruption is evaluated in order')
        if not d.get('m_CanTransitionToSelf', 1):
            out['canTransitionToSelf'] = False
        out['_solo'] = bool(d.get('m_Solo'))
        return out

    @staticmethod
    def _solo(ts):
        if any(t['_solo'] for t in ts):
            ts = [t for t in ts if t['_solo']]
        for t in ts:
            t.pop('_solo', None)
        return ts

    def layer(self, L, index, synced=None):
        name = L.get('m_Name') or 'Layer %d' % index
        machine = fid((L.get('m_StateMachine') or {}).get('fileID'))
        out = {'name': name}
        if index > 0:
            out['weight'] = Y.num(L.get('m_DefaultWeight'), 0)
            out['weightParameter'] = self.prefix + name
        if int(Y.num(L.get('m_BlendingMode'), 0)) == 1:
            out['blending'] = 'additive'
        mask_ref = L.get('m_Mask') or {}
        if ref_key(mask_ref, self.guid):
            mask, mname = self.load_mask_ref(mask_ref)
            if mask is None:
                self.note('layer "%s": avatar mask "%s" has no transform list (humanoid body mask) and is ignored' % (name, mname))
            else:
                out['mask'] = mask
        if L.get('m_IKPass'):
            self.note('layer "%s": IK pass (OnAnimatorIK) is not imported' % name)
        sync_motions = {}
        if synced is not None:
            out['_synced'] = synced
            for m in L.get('m_Motions') or []:
                sid = fid((m.get('m_State') or {}).get('fileID'))
                sync_motions[sid] = m.get('m_Motion')
            if L.get('m_SyncedLayerAffectsTiming'):
                self.note('layer "%s": synced timing is not shared between layers' % name)
        states, any_ids, entry = {}, [], []
        default = self.flatten(machine, '', states, any_ids, entry)
        for e in entry:
            self.note(e)
        if not states:
            out['states'] = [{'name': 'Empty', 'motion': None}]
            return out
        names = dict(states)
        out['states'] = []
        for sid, sname in states.items():
            sd = self.by_id[sid].data
            st = {'name': sname}
            st['motion'] = self.motion(sync_motions[sid] if sid in sync_motions else sd.get('m_Motion'))
            if Y.num(sd.get('m_Speed'), 1) != 1:
                st['speed'] = Y.num(sd.get('m_Speed'), 1)
            if sd.get('m_SpeedParameterActive') and sd.get('m_SpeedParameter'):
                st['speedParameter'] = sd['m_SpeedParameter']
            if sd.get('m_TimeParameterActive') and sd.get('m_TimeParameter'):
                st['timeParameter'] = sd['m_TimeParameter']
            if Y.num(sd.get('m_CycleOffset'), 0):
                st['cycleOffset'] = Y.num(sd.get('m_CycleOffset'), 0)
            if sd.get('m_CycleOffsetParameterActive') and sd.get('m_CycleOffsetParameter'):
                st['cycleOffsetParameter'] = sd['m_CycleOffsetParameter']
            if sd.get('m_Mirror') or sd.get('m_MirrorParameterActive'):
                self.note('state "%s": mirror is not supported' % sname)
            if sd.get('m_IKOnFeet'):
                self.note('state "%s": foot IK (humanoid) is not imported' % sname)
            if sd.get('m_StateMachineBehaviours'):
                self.note('state "%s": state machine behaviours (scripts) are not imported' % sname)
            if sd.get('m_Tag'):
                st['tag'] = sd['m_Tag']
            ts = [self.transition(fid(t.get('fileID')), names, default) for t in sd.get('m_Transitions') or []]
            st['transitions'] = self._solo([t for t in ts if t])
            out['states'].append(st)
        anys = [self.transition(tid, names, default) for tid in any_ids]
        if anys:
            out['anyState'] = self._solo([t for t in anys if t])
        out['default'] = names.get(default, out['states'][0]['name'])
        return out

    def convert(self):
        ctrl = next((d for d in self.docs if d.class_id == C_CONTROLLER), None)
        if ctrl is None:
            raise ValueError('%s: no AnimatorController' % self.path)
        params, triggers, types = {}, [], {}
        for p in ctrl.data.get('m_AnimatorParameters') or []:
            n, t = p.get('m_Name') or '', int(Y.num(p.get('m_Type'), 1))
            types[n] = PARAM_TYPES.get(t, 'float')
            if t == 1:
                params[n] = Y.num(p.get('m_DefaultFloat'), 0)
            elif t == 3:
                params[n] = Y.num(p.get('m_DefaultInt'), 0)
            else:
                params[n] = 1 if p.get('m_DefaultBool') and t == 4 else 0
            if t == 9:
                triggers.append(n)
        raw = ctrl.data.get('m_AnimatorLayers') or []
        layers = []
        for i, L in enumerate(raw):
            si = int(Y.num(L.get('m_SyncedLayerIndex'), -1))
            if 0 <= si < len(raw) and si != i:
                src = dict(raw[si])
                src.update({k: L.get(k) for k in ('m_Name', 'm_Mask', 'm_BlendingMode', 'm_DefaultWeight', 'm_IKPass', 'm_Motions', 'm_SyncedLayerAffectsTiming')})
                layers.append(self.layer(src, i, synced=si))
            else:
                layers.append(self.layer(L, i))
        for L in layers:
            L.pop('_synced', None)
            if 'weightParameter' in L:
                params[L['weightParameter']] = L['weight']
        node = {'layers': layers}
        if triggers:
            node['triggers'] = triggers
        graph = {'parameters': params, 'nodes': [{'id': 'animator', 'kind': 'controller', 'controller': node}], 'output': 'animator'}
        return graph, types


def override_map(docs, owner_guid):
    """(base controller ref, {original clip key: override clip key}) of an AnimatorOverrideController."""
    d = next((x for x in docs if x.class_id == C_OVERRIDE), None)
    if d is None:
        return None, {}
    out = {}
    for c in d.data.get('m_Clips') or []:
        a, b = ref_key(c.get('m_OriginalClip'), owner_guid), ref_key(c.get('m_OverrideClip'), owner_guid)
        if a and b:
            out[a] = b
    return d.data.get('m_Controller') or {}, out
