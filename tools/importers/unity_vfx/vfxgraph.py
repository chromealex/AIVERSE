#!/usr/bin/env python3
"""Unity VFX Graph (.vfx) -> AIVERSE particle effects, for the block / operator subset games actually use.

    python3 tools/importers/unity_vfx/vfxgraph.py --source <file.vfx | prefab with a VisualEffect> --game <project>
        [--name slash] [--scale 1] [--json] [--dry-run]

One VFX particle system (Initialize -> Update -> Output contexts sharing one particle data) becomes one engine
emitter per output context (outputs simulate separately in the engine: their particles do not match one to one).
Spawner contexts give rate / bursts / loop / delay, GPU events (Trigger Event blocks) become sub-emitters (the
receiving system is written as its own effect `<name>_<label>`). Exposed properties take the values the
VisualEffect component overrides (prefab import). Everything else is reported by name: unknown nodes, blocks without
an engine equivalent, values that depend on time or on other attributes. The model / evaluation is
vfxgraph_model.py; docs: docs/particles.md ("VFX Graph").
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert as cv  # noqa: E402
import vfxgraph_model as vm  # noqa: E402
from materials import Meshes, sanitize, Textures, MaterialInfo  # noqa: E402
from unity_scene import Assets, Report, SysLog, project_roots  # noqa: E402
from unity_yaml import get, hex_color, num  # noqa: E402
from vfxgraph_model import DeltaTime, Life, Rand, Unknown, is_num  # noqa: E402

BUILTIN_TEX_GUID = '0000000000000000f000000000000000'
BLEND = {0: 'additive', 1: 'alpha', 2: 'premultiplied', 3: 'opaque'}
SHAPES = {0: 'sphere', 1: 'box', 2: 'cone', 3: 'torus', 4: 'circle', 5: 'line', 6: 'sdf'}
ORIENT = {0: 'camera', 1: 'camera', 2: 'camera', 3: 'camera', 5: 'upright', 6: 'velocity'}
RAD = 180.0 / math.pi
P = cv.P
PT = cv.mt(cv.P)


def rv(v, d=4):
    return cv.rv(round(v, d))


def to_eff(v):
    """Unity vector -> effect frame (P)."""
    v = vm.unwrap(v)
    if not isinstance(v, tuple):
        v = (0.0, 0.0, 0.0)
    return cv.mv(P, tuple(v[:3]) + (0.0,) * (3 - len(v[:3])))


def rng(v, lo=None, hi=None):
    """Number / Rand -> engine range JSON (number or [min, max]); None for anything else."""
    def c(x):
        x = float(x)
        if lo is not None:
            x = max(lo, x)
        if hi is not None:
            x = min(hi, x)
        return rv(x)
    if is_num(v):
        return c(v)
    if isinstance(v, Rand) and is_num(v.lo) and is_num(v.hi):
        a, b = sorted((c(v.lo), c(v.hi)))
        return a if a == b else [a, b]
    if isinstance(v, Life) and not v.gradient and v.keys != 'age':
        return {'curve': [[rv(t, 3), c(x * v.k)] for t, x in v.keys]}
    return None


def comp(v, i):
    """Component i of a vector / Rand of vectors / Composite."""
    if isinstance(v, vm.Composite):
        return v.parts[i] if i < len(v.parts) else 0.0
    if isinstance(v, tuple):
        return v[i] if i < len(v) else 0.0
    if isinstance(v, Rand) and isinstance(v.lo, tuple):
        return Rand(v.lo[i] if i < len(v.lo) else 0.0, v.hi[i] if i < len(v.hi) else 0.0)
    return v


def mid(v):
    if isinstance(v, Rand):
        return vm._vecop(v.lo, v.hi, lambda a, b: (a + b) * 0.5)
    return v


class System:
    """One particle system (an Initialize context with its spawners, updates and outputs)."""

    def __init__(self, conv, init, label):
        self.conv, self.g, self.init, self.label = conv, conv.g, init, label
        self.log = SysLog(conv.rep, '%s: %s' % (conv.rel, label))
        self.attr = {}          # initial attribute values
        self.over = {}          # attribute -> Life (over life), or rates ('spin', 'force', 'velocity')
        self.em = {}            # emitter keys shared by the outputs
        self.subs = []          # (on, gpu event context, count, rate)

    def unsupported(self, block, why):
        self.log.drop(block, why)

    def approx(self, block, why):
        self.log.approx(block, why)


class Converter:
    def __init__(self, path, assets, rep, name, game_dir, dry_run, scale=1.0, overrides=None, textures=None, meshes=None,
                 offset=(0.0, 0.0, 0.0)):
        self.path, self.assets, self.rep, self.name, self.scale = path, assets, rep, name, scale
        self.rel = os.path.basename(path)
        self.g = vm.Graph(path, overrides)
        self.tex = textures or Textures(game_dir, assets, rep, dry_run)
        self.meshes = meshes or Meshes(game_dir, assets, rep, dry_run)
        self.offset = offset
        for guid, cls in sorted(self.g.unknown.items()):
            rep.notes.append('%s: node script %s not in the VFX Graph table: classified as %s by its fields' % (self.rel, guid, cls))

    # ---------------------------------------------------------------------------------- systems
    def systems(self):
        g = self.g
        inits = [c for c in g.contexts() if g.kind(c) == 'VFXBasicInitialize']
        out, labels = [], set()
        for i, init in enumerate(inits):
            label = self.label_of(init, i)
            base, k = label, 2
            while label in labels:
                label = '%s_%d' % (base, k)
                k += 1
            labels.add(label)
            out.append((init, label))
        return out

    def label_of(self, init, i):
        g = self.g
        names = [str(c.data.get('m_Label') or '') for c in [init] + self.chain(init)[1] if str(c.data.get('m_Label') or '').strip()]
        return sanitize(names[0].lower().replace(' ', '_'), 24) if names else 'system%d' % (i + 1)

    def chain(self, init):
        """(update contexts, output contexts) after an Initialize."""
        g = self.g
        updates, outputs, seen, todo = [], [], set(), g.flow_out(init)
        while todo:
            c = todo.pop(0)
            if c.file_id in seen:
                continue
            seen.add(c.file_id)
            k = g.kind(c)
            if k == 'VFXBasicUpdate':
                updates.append(c)
                todo += g.flow_out(c)
            elif k in vm.OUTPUTS:
                outputs.append(c)
        return updates, outputs

    def convert(self):
        g = self.g
        if g.graph is None:
            self.rep.warnings.append('%s: no VFX graph in the file' % self.rel)
            return []
        systems = self.systems()
        self.gpu_triggers = {}   # GPU event context id -> (trigger mode, trigger slot values)
        for d in g.docs:
            if g.kind(d) != 'TriggerEvent':
                continue
            for o in g.slots(d, 'm_OutputSlots'):
                for l in o.data.get('m_LinkedSlots') or []:
                    tgt = g.node(l)
                    ctx_, _ = g.owner(tgt) if tgt is not None else (None, None)
                    if ctx_ is not None:
                        self.gpu_triggers[ctx_.file_id] = (int(num(d.data.get('mode'), 0)), g.slot_values(d))
        gpu_target = {}   # GPU event context id -> effect name of the systems it starts
        for init, label in systems:
            for sp in g.flow_in(init):
                if g.kind(sp) == 'VFXBasicGPUEvent':
                    gpu_target[sp.file_id] = sanitize('%s_%s' % (self.name, label), 44)
        effects = {}
        for init, label in systems:
            s = System(self, init, label)
            started_by_gpu = any(g.kind(sp) == 'VFXBasicGPUEvent' for sp in g.flow_in(init))
            emitters = self.map_system(s)
            for e in emitters:
                for sub in e.pop('_subs', []):
                    tgt = gpu_target.get(sub['ctx'])
                    if tgt is None:
                        s.unsupported('trigger event', 'GPU event goes to no particle system')
                        continue
                    o = {'on': sub['on'], 'effect': tgt}
                    e.setdefault('subEmitters', []).append(o)
            target = gpu_target.get(next((sp.file_id for sp in g.flow_in(init) if g.kind(sp) == 'VFXBasicGPUEvent'), None)) \
                if started_by_gpu else self.name
            effects.setdefault(target, []).extend(emitters)
            self.rep.systems.append({'system': '%s: %s' % (self.rel, label), 'modules': s.log.modules})
        out = []
        for ename, ems in effects.items():
            if not ems:
                continue
            if len(ems) > 16:
                self.rep.dropped.append('%s: %d outputs, the engine keeps 16 emitters per effect' % (ename, len(ems)))
                ems = ems[:16]
            out.append((ename, {'comment': 'Imported from Unity VFX Graph %s by tools/importers/unity_vfx/vfxgraph.py' % self.rel,
                                'emitters': ems}))
        out.sort(key=lambda x: x[0] != self.name)
        return out

    # ---------------------------------------------------------------------------------- one system
    def map_system(self, s):
        g = self.g
        data = g.data(s.init)
        d = data.data if data is not None else {}
        cap = int(num(d.get('capacity'), 0))
        if cap:
            s.em['maxParticles'] = min(100000, cap)
        if int(num(d.get('m_Space'), 1)) == 0:
            s.em['space'] = 'local'
        strip = int(num(d.get('dataType'), 0)) == 1
        self.map_spawners(s)
        for b in g.children(s.init):
            if g.enabled(b):
                self.map_block(s, b, 'init')
        updates, outputs = self.chain(s.init)
        for u in updates:
            if not int(num(u.data.get('ageParticles'), 1)):
                s.approx('update', 'particles do not age: they live their lifetime anyway')
            for b in g.children(u):
                if g.enabled(b):
                    self.map_block(s, b, 'update')
        if not updates:
            s.approx('update', 'no Update context: particles still age and die in the engine')
        if not outputs:
            s.unsupported('output', 'no output context: nothing drawn (kept hidden for its sub-emitters)')
            em = self.finish(s, None, strip)
            em['hidden'] = True
            return [em]
        return [self.finish(s, o, strip) for o in outputs]

    def map_spawners(self, s):
        g = self.g
        rate, bursts, per_meter = 0.0, [], 0.0
        loop, duration, delay = True, 0.0, None
        n = 0
        for sp in g.flow_in(s.init):
            k = g.kind(sp)
            if k == 'VFXBasicGPUEvent':
                trig = self.gpu_triggers.get(sp.file_id)
                if trig is None:
                    s.unsupported('spawn', 'GPU event without a trigger block')
                    continue
                mode, tv = trig
                if mode in (2, 3):
                    r = mid(tv.get('Rate', 1.0))
                    if is_num(r):
                        if mode == 2:
                            rate += r
                        else:
                            per_meter += r
                else:
                    c = mid(tv.get('count', 1.0))
                    bursts.append({'time': 0, 'count': int(round(c)) if is_num(c) else 1})
                s.log.ok('spawn', 'started by a GPU event (sub-emitter of another system)')
                n += 1
                continue
            if k != 'VFXBasicSpawner':
                continue
            n += 1
            for ev in g.flow_in(sp):
                s.approx('spawn', 'spawner started by event context %s: plays on OnPlay' % (ev.data.get('m_Label') or 'event'))
            v = g.slot_values(sp)
            ld, lc, dl = (int(num(sp.data.get(x), 0)) for x in ('loopDuration', 'loopCount', 'delayBeforeLoop'))
            dur = mid(v.get('LoopDuration', 1.0)) if ld else 0.0
            count = mid(v.get('LoopCount', 1.0)) if lc else 0
            if ld and is_num(dur):
                duration = max(duration, float(dur))
            if ld and lc and is_num(count):
                loop = False
                if count > 1:
                    duration = max(duration, float(dur) * count)
                    s.approx('spawn', 'loop count %d: one duration of %g s x %d' % (count, dur, count))
            if dl:
                delay = rng(v.get('DelayBeforeLoop', 0.0), 0, 60)
            if int(num(sp.data.get('delayAfterLoop'), 0)):
                s.approx('spawn', 'delay after loop dropped')
            for b in g.children(sp):
                if not g.enabled(b):
                    continue
                bk = g.kind(b)
                bv = g.slot_values(b)
                if bk == 'VFXSpawnerConstantRate':
                    r = mid(bv.get('Rate', 10.0))
                    if is_num(r):
                        rate += r
                        s.log.ok('spawn', 'constant rate %g' % r)
                    else:
                        s.unsupported('spawn', 'rate %r' % (r,))
                elif bk == 'VFXSpawnerVariableRate':
                    r = bv.get('Rate')
                    if isinstance(r, tuple):
                        rate += (r[0] + r[1]) * 0.5
                        s.approx('spawn', 'variable rate %g..%g -> its average' % (r[0], r[1]))
                elif bk == 'VFXSpawnerBurst':
                    c = bv.get('Count', 1.0)
                    if isinstance(c, tuple):
                        c = Rand(c[0], c[1])
                    dly = bv.get('Delay', 0.0)
                    dly = mid(Rand(dly[0], dly[1]) if isinstance(dly, tuple) else dly)
                    cj = rng(c, 0, 2000)
                    if cj is None:
                        s.unsupported('spawn', 'burst count %r' % (c,))
                        continue
                    if isinstance(cj, float):
                        cj = int(round(cj))
                    elif isinstance(cj, list):
                        cj = [int(round(x)) for x in cj]
                    if int(num(b.data.get('repeat'), 0)):
                        bursts.append({'time': 0, 'count': cj, 'cycles': 0, 'interval': rv(max(0.01, float(dly or 0.1)))})
                        s.log.ok('spawn', 'periodic burst every %g s' % float(dly or 0))
                    else:
                        bursts.append({'time': rv(float(dly or 0)), 'count': cj})
                        s.log.ok('spawn', 'burst %s' % (cj,))
                elif bk == 'VFXSpawnerCustomWrapper' and 'OverDistance' in str(get(b.data, 'm_customType', 'm_SerializableType', default='')):
                    pm = mid(bv.get('RatePerUnit', 1.0))
                    if is_num(pm):
                        per_meter += pm
                        s.log.ok('spawn', 'spawn over distance %g per unit' % pm)
                else:
                    s.unsupported('spawn', 'spawner block %s' % bk)
        if n == 0 and not bursts:
            s.unsupported('spawn', 'no spawner: nothing is emitted')
        bursts = [b for b in bursts if b is not None]
        em = s.em
        if rate > 0:
            em['rate'] = rv(min(10000, rate / 1))
        if per_meter > 0:
            em['perMeter'] = rv(per_meter / max(self.scale, 1e-6))
        if len(bursts) == 1 and set(bursts[0]) == {'time', 'count'} and bursts[0]['time'] == 0 and isinstance(bursts[0]['count'], int):
            em['burst'] = bursts[0]['count']
        elif bursts:
            em['bursts'] = bursts[:8]
        em['duration'] = rv(duration if duration > 0 else (1.0 if rate or per_meter else 0.1))
        em['loop'] = bool(loop and n > 0 and (rate > 0 or per_meter > 0 or any(b.get('cycles') == 0 for b in bursts) or duration > 0)
                          and not (duration > 0 and not (rate or per_meter) and all('cycles' not in b for b in bursts)))
        if delay not in (None, 0):
            em['delay'] = delay

    # ---------------------------------------------------------------------------------- blocks
    def map_block(self, s, b, where):
        g = self.g
        k = g.kind(b)
        if k == 'SetAttribute':
            return self.set_attribute(s, b, where)
        if k == 'AttributeFromCurve':
            return self.attribute_curve(s, b, where)
        if k == 'ColorOverLife':
            v = g.slot_values(b).get('gradient')
            s.over['color'] = Life(vm.gradient_points(v), 1.0, True)
            s.log.ok('color', 'colour over life')
            return
        if k == 'PositionShape':
            return self.position_shape(s, b)
        if k == 'VelocityDirection':
            return self.velocity_direction(s, b)
        if k == 'Gravity':
            f = vm.unwrap(g.slot_values(b).get('Force', (0.0, -9.81, 0.0)))
            if isinstance(f, tuple):
                s.em['gravity'] = [rv(f[0] * self.scale), rv(f[1] * self.scale), rv(-f[2] * self.scale)]
                s.log.ok('forces', 'gravity')
            else:
                s.unsupported('forces', 'gravity %r' % (f,))
            return
        if k == 'Turbulence':
            v = g.slot_values(b)
            inten, freq = mid(v.get('Intensity', 1.0)), v.get('frequency', 1.0)
            noise = {'strength': rv(min(100, abs(float(inten if is_num(inten) else 1.0)) * self.scale)),
                     'frequency': rv(max(0.0001, float(freq) if is_num(freq) else 1.0) / max(self.scale, 1e-6))}
            oc = v.get('octaves', 1.0)
            if is_num(oc) and int(oc) > 1:
                noise['octaves'] = min(4, int(oc))
            s.em['noise'] = noise
            drag = v.get('Drag')
            if is_num(drag) and drag > 0 and int(num(b.data.get('Mode'), 0)) == 1:
                s.em['drag'] = rv(min(100, drag))
            s.approx('noise', 'turbulence -> engine curl noise (field transform, roughness, lacunarity dropped)')
            return
        if k == 'ConformToSphere':
            v = g.slot_values(b)
            sph = v.get('Sphere') or {}
            centre = sph.get('center', (0.0, 0.0, 0.0)) if isinstance(sph, dict) else (0.0, 0.0, 0.0)
            speed = mid(v.get('attractionSpeed', 5.0))
            f = {'position': [rv(x * self.scale) for x in to_eff(centre if isinstance(centre, tuple) else (0, 0, 0))],
                 'radius': rv(min(1000, max(0.01, num(sph.get('radius') if isinstance(sph, dict) else 1, 1) * self.scale * 4))),
                 'gravity': rv(min(1000, float(speed if is_num(speed) else 5.0) * self.scale))}
            s.em['externalForces'] = 1
            if f not in self.fields:
                self.fields.append(f)
            s.approx('forces', 'conform to sphere -> a gravity force field at its centre (no sticking to the surface)')
            return
        if k == 'Collision':
            v = g.slot_values(b)
            col = {}
            shape = int(num(b.data.get('shape'), 0))
            bounce, fr, ll = v.get('Bounce'), v.get('Friction'), v.get('LifetimeLoss')
            if is_num(bounce):
                col['bounce'] = rv(min(1, max(0, bounce)))
            if is_num(fr):
                col['dampen'] = rv(min(1, max(0, fr)))
            if is_num(ll):
                col['lifeLoss'] = rv(min(1, max(0, ll)))
            pl = v.get('Plane')
            if shape == 1 and isinstance(pl, dict):
                pos, nrm = pl.get('position', (0, 0, 0)), pl.get('normal', (0, 1, 0))
                if isinstance(pos, tuple) and isinstance(nrm, tuple):
                    col['ground'] = False
                    col['planes'] = [{'point': [rv(x * self.scale) for x in to_eff(pos)], 'normal': [rv(x) for x in to_eff(nrm)]}]
                    s.log.ok('collision', 'plane')
            else:
                s.approx('collision', '%s collider -> the ground' % {0: 'sphere', 3: 'depth buffer'}.get(shape, 'shape %d' % shape))
            s.em['collision'] = col
            return
        if k == 'TriggerEvent':
            mode = int(num(b.data.get('mode'), 0))
            on = 'death' if mode == 1 else 'birth'
            for o in g.slots(b, 'm_OutputSlots'):
                for l in o.data.get('m_LinkedSlots') or []:
                    tgt = g.node(l)
                    ctx, _ = g.owner(tgt) if tgt is not None else (None, None)
                    if ctx is not None and g.kind(ctx) == 'VFXBasicGPUEvent':
                        s.subs.append({'on': on, 'ctx': ctx.file_id})
            if mode in (2, 3):
                s.approx('sub-emitters', 'trigger event %s -> a "birth" sub-emitter (its own rate)' % ('over time' if mode == 2 else 'over distance'))
            elif mode == 0:
                s.approx('sub-emitters', 'trigger event always -> a "birth" sub-emitter')
            else:
                s.log.ok('sub-emitters', 'trigger event on die')
            return
        if k == 'Orient':
            mode = int(num(b.data.get('mode'), 0))
            if mode in ORIENT:
                s.em['align'] = ORIENT[mode]
                if mode in (2, 3):
                    s.approx('orient', 'look at position / line -> camera facing')
            else:
                s.approx('orient', 'orient mode %d -> camera facing' % mode)
            return
        if k == 'SubpixelAA':
            return
        s.unsupported('block', '%s block (%s context)' % (k, where))

    def set_attribute(self, s, b, where):
        g = self.g
        a = str(b.data.get('attribute') or '')
        comp_ = int(num(b.data.get('Composition'), 0))
        rnd = int(num(b.data.get('Random'), 0))
        if int(num(b.data.get('Source'), 0)):
            if a == 'position':
                s.log.ok(a, 'inherited from the parent particle (sub-emitter position)')
            else:
                s.approx(a, 'inherited from the parent particle (source attribute) -> the system\'s own value')
            return
        vals = g.slot_values(b)
        if rnd:
            lst = list(vals.values())
            lo = vals.get('A', lst[0] if lst else None)
            hi = vals.get('B', lst[1] if len(lst) > 1 else lo)
            v = vm.Rand(mid(lo), mid(hi))
            if isinstance(v.lo, (Unknown, type(None))) or isinstance(v.hi, (Unknown, type(None))):
                v = Unknown('random bounds')
        else:
            v = next(iter(vals.values()), None) if vals else None
        if isinstance(v, Unknown):
            s.unsupported(a, '%s value: %s' % (where, v.why))
            return
        if where == 'update':
            if comp_ == 1 and isinstance(v, DeltaTime):
                s.over[a + '+'] = v.k
                return
            if comp_ == 1:
                if isinstance(v, vm.Composite):
                    v = vm.Composite([vm.arith('Multiply', p_, 60.0) for p_ in v.parts])
                elif isinstance(v, Life):
                    v = vm.arith('Multiply', v, 60.0)
                else:
                    v = vm._vecop(mid(v), 60.0, lambda x, y: x * y)
                s.over[a + '+'] = v
                s.approx(a, 'added every update without deltaTime: taken as 60 updates per second')
                return
            if comp_ == 2 and isinstance(v, Life):
                s.over[a] = v
                return
            if comp_ == 0 and isinstance(v, Life):
                s.over[a] = v
                s.over[a + '='] = True
                return
            s.approx(a, 'set every update (composition %d) -> initial value' % comp_)
        if where == 'output' and isinstance(v, Life):
            s.over[a] = v
            if comp_ == 0:
                s.over[a + '='] = True
            return
        cur = s.attr.get(a)
        if comp_ == 0 or cur is None:
            s.attr[a] = v
        elif comp_ in (1, 2, 3):
            op = {1: 'Add', 2: 'Multiply', 3: 'Add'}[comp_]
            r = vm.arith(op, cur, v)
            if comp_ == 3:
                s.approx(a, 'blend composition -> add')
            s.attr[a] = r
        s.log.ok(a, 'set (%s)' % where)

    def attribute_curve(self, s, b, where):
        g = self.g
        a = str(b.data.get('attribute') or '')
        if int(num(b.data.get('SampleMode'), 0)) != 0:
            s.unsupported(a, 'attribute from curve sampled by %s (only over life maps)' %
                          {1: 'speed', 2: 'random', 3: 'random per particle', 4: 'custom'}.get(int(num(b.data.get('SampleMode'), 0)), '?'))
            return
        comp_ = int(num(b.data.get('Composition'), 0))
        vals = g.slot_values(b)
        if a == 'color':
            gr = vals.get('Color')
            if isinstance(gr, Unknown):
                s.unsupported(a, gr.why)
                return
            s.over['color'] = Life(vm.gradient_points(gr), 1.0, True)
            if comp_ == 0:
                s.over['color='] = True
            if int(num(b.data.get('AlphaComposition'), comp_)) not in (0, 2):
                s.approx('color', 'alpha composition add / blend -> multiply')
            s.log.ok('color', 'colour over life')
            return
        cur = None
        for key in (a.capitalize(), a, 'Size', 'Alpha', 'TexIndex', 'Scale_x', 'Scale_y'):
            if key in vals:
                cur = vals[key]
                break
        if cur is None and vals:
            cur = next(iter(vals.values()))
        if isinstance(cur, Unknown) or cur is None:
            s.unsupported(a, 'curve %s' % (cur.why if isinstance(cur, Unknown) else 'missing'))
            return
        keys = vm.curve_points(cur)
        if a in ('size', 'alpha', 'texIndex', 'scale', 'angle'):
            if a == 'scale':
                ch = int(num(b.data.get('channels'), 6))
                if ch == 1:
                    s.over['scaleY'] = Life(keys)
                else:
                    s.over['size'] = Life(keys)
                    s.approx('scale', 'scale over life on several axes -> size over life')
            else:
                s.over[a] = Life(keys)
            if comp_ == 0:
                s.over[a + '='] = True
            elif comp_ == 1:
                s.approx(a, 'add composition -> multiply')
            s.log.ok(a, '%s over life' % a)
            return
        s.unsupported(a, 'attribute %s over life' % a)

    def position_shape(self, s, b):
        g = self.g
        em = s.em
        shape = SHAPES.get(int(num(b.data.get('shape'), 0)), 'sphere')
        mode = int(num(b.data.get('positionMode'), 0))   # 0 surface, 1 volume, 2 / 3 thickness
        vals = g.slot_values(b)
        if int(num(b.data.get('spawnMode'), 0)) == 1:
            s.approx('shape', 'custom spawn position (a slot value) -> random')
        th = vals.get('Thickness')

        def tf(v):
            t = v.get('transform') if isinstance(v, dict) else None
            t = t if isinstance(t, dict) else {}
            pos = t.get('position', (0.0, 0.0, 0.0))
            ang = t.get('angles', (0.0, 0.0, 0.0))
            sc = t.get('scale', (1.0, 1.0, 1.0))
            ok = all(isinstance(x, tuple) for x in (pos, ang, sc))
            if not ok:
                s.approx('shape', 'shape transform driven by operators -> identity')
            return (pos, ang, sc) if ok else ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))

        def num_of(x, default):
            x = mid(x)
            return float(x) if is_num(x) else default

        def turn(M):
            if not cv.is_identity(M):
                em['shapeRotation'] = [rv(x, 3) for x in cv.euler_zxy(M)]

        def place(pos):
            off = to_eff(pos)
            if vm.v_len(off) > 1e-5:
                em['offset'] = [rv(x * self.scale) for x in off]

        if shape in ('sphere', 'circle', 'torus', 'cone'):
            key = {'sphere': 'arcSphere', 'circle': 'arcCircle', 'torus': 'arcTorus', 'cone': 'arcCone'}[shape]
            v = vals.get(key) or {}
            inner = v.get({'sphere': 'sphere', 'circle': 'circle', 'torus': 'torus', 'cone': 'cone'}[shape]) if isinstance(v, dict) else {}
            inner = inner if isinstance(inner, dict) else {}
            arc = num_of(v.get('arc', 2 * math.pi) if isinstance(v, dict) else 2 * math.pi, 2 * math.pi) * RAD
            pos, ang, sc = tf(inner)
            R = cv.mat_zxy(ang)
            k = max(abs(x) for x in sc) * self.scale
            place(pos)
            if arc < 359.9:
                em['arc'] = rv(min(360, max(0, arc)), 2)
            if shape == 'sphere':
                em['shape'] = 'sphere'
                em['radius'] = rv(num_of(inner.get('radius', 1.0), 1.0) * k)
                turn(cv.mm(P, cv.mm(R, PT)) if arc >= 359.9 else cv.mm(P, cv.mm(R, cv.arc_frame(arc))))
            elif shape == 'circle':
                em['shape'] = 'ring' if mode == 0 else 'disc'
                em['radius'] = rv(num_of(inner.get('radius', 1.0), 1.0) * k)
                turn(cv.mm(P, cv.mm(R, cv.arc_frame(arc))))
            elif shape == 'torus':
                em['shape'] = 'donut'
                em['radius'] = rv(num_of(inner.get('majorRadius', 1.0), 1.0) * k)
                em['donutRadius'] = rv(num_of(inner.get('minorRadius', 0.1), 0.1) * k)
                turn(cv.mm(P, cv.mm(R, cv.arc_frame(arc))))
            else:
                em['shape'] = 'cone'
                br, tr = num_of(inner.get('baseRadius', 1.0), 1.0), num_of(inner.get('topRadius', 0.1), 0.1)
                h = num_of(inner.get('height', 1.0), 1.0)
                em['radius'] = rv(br * k)
                em['spread'] = rv(min(90, max(0, math.degrees(math.atan2(tr - br, max(1e-4, h))))), 2)
                if mode == 1:
                    em['length'] = rv(h * k)
                turn(cv.mm(P, cv.mm(R, PT)))
            if mode == 0 and shape != 'circle':
                em['emitFrom'] = 'shell'
        elif shape == 'box':
            v = vals.get('Box') or vals.get('box') or {}
            v = v if isinstance(v, dict) else {}
            centre, ang, size = v.get('center', (0.0, 0.0, 0.0)), v.get('angles', (0.0, 0.0, 0.0)), v.get('size', (1.0, 1.0, 1.0))
            if not all(isinstance(x, tuple) for x in (centre, ang, size)):
                s.approx('shape', 'box driven by operators -> unit box')
                centre, ang, size = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
            em['shape'] = 'box'
            sz = to_eff(size)
            em['box'] = [rv(abs(x) * self.scale) for x in sz]
            place(centre)
            turn(cv.mm(P, cv.mm(cv.mat_zxy(ang), PT)))
            if mode == 0:
                em['emitFrom'] = 'shell'
        elif shape == 'line':
            v = vals.get('line') or vals.get('Line') or {}
            a_, b_ = (v.get('start', (0.0, 0.0, 0.0)), v.get('end', (1.0, 0.0, 0.0))) if isinstance(v, dict) else ((0.0,) * 3, (1.0, 0, 0))
            if isinstance(a_, tuple) and isinstance(b_, tuple):
                ea, eb = to_eff(a_), to_eff(b_)
                d = tuple(y - x for x, y in zip(ea, eb))
                L = vm.v_len(d)
                em['shape'] = 'line'
                em['radius'] = rv(L * 0.5 * self.scale)
                place(tuple((x + y) * 0.5 for x, y in zip(a_, b_)))
                if L > 1e-6:
                    turn(cv.rot_between((1.0, 0.0, 0.0), tuple(x / L for x in d)))
        else:
            s.unsupported('shape', '%s position shape' % shape)
            return
        if mode in (2, 3) and is_num(th):
            if mode == 3:
                em['thickness'] = rv(min(1, max(0, th)))
            else:
                r = num(em.get('radius'), 1) or 1
                em['thickness'] = rv(min(1, max(0, th * self.scale / r)))
        if int(num(b.data.get('applyOrientation'), 1)) & 1:
            s.shape_direction = True
        s.log.ok('shape', shape)

    def velocity_direction(self, s, b):
        g = self.g
        vals = g.slot_values(b)
        sp = vals.get('Speed') if int(num(b.data.get('speedMode'), 0)) == 0 else Rand(mid(vals.get('MinSpeed', 0.0)), mid(vals.get('MaxSpeed', 1.0)))
        r = rng(sp, -500, 500)
        if r is None:
            s.unsupported('velocity', 'speed %r' % (sp,))
            return
        s.speed = sp
        blend = mid(vals.get('DirectionBlend', 1.0))
        blend = float(blend) if is_num(blend) else 1.0
        if 'center' in vals:
            s.dir_mode = ('radial', blend)
        elif 'Direction' in vals:
            dv = vals.get('Direction')
            s.dir_mode = ('direction', blend, dv if isinstance(dv, tuple) else (0.0, 1.0, 0.0))
        else:
            s.dir_mode = ('random', blend)
        if int(num(b.data.get('composition'), 0)) != 0:
            s.approx('velocity', 'velocity composition add / multiply -> overwrite')
        s.log.ok('velocity', 'velocity from direction and speed')

    # ---------------------------------------------------------------------------------- one emitter
    def finish(self, s, out, strip):
        """Emitter of one output: the system's state plus the output's own blocks (restored afterwards)."""
        saved = (dict(s.attr), dict(s.over), dict(s.em), list(s.subs))
        try:
            if out is not None:
                for b in self.g.children(out):
                    if self.g.enabled(b):
                        self.map_block(s, b, 'output')
            return self._emitter(s, out, strip)
        finally:
            s.attr, s.over, s.em, s.subs = saved

    def _emitter(self, s, out, strip):
        g = self.g
        em = dict(s.em)
        over = s.over
        attr = s.attr
        log = s.log
        # life / size / colour
        life = attr.get('lifetime', 1.0)
        em['life'] = rng(life, 0.01, 600) if rng(life) is not None else 1.0
        size = attr.get('size', 0.1)
        scale = attr.get('scale', (1.0, 1.0, 1.0))
        sx, sy = comp(scale, 0), comp(scale, 1)
        sz_ = vm.arith('Multiply', size, mid(sx)) if not isinstance(size, Unknown) else size
        em['size'] = rng(sz_, 0, 1000) if rng(sz_) is not None else 0.1
        if isinstance(sz_, Unknown):
            log.approx('size', sz_.why)
        msx, msy = mid(sx), mid(sy)
        if is_num(msx) and is_num(msy) and msx and abs(msy / msx - 1) > 1e-3:
            em['aspect'] = rv(min(100, max(0.01, abs(msy / msx))))
        col = attr.get('color', (1.0, 1.0, 1.0))
        alpha = attr.get('alpha', 1.0)
        em.update(self.start_color(col, alpha, log))
        em['color'] = self.color_keys(over, log)
        if 'size' in over:
            k = over['size']
            keys = [(t, v * k.k) for t, v in k.keys]
            if over.get('size=') and is_num(mid(size)) and mid(size):
                base = float(mid(size)) * (float(msx) if is_num(msx) else 1)
                keys = [(t, v / base * (float(msx) if is_num(msx) else 1)) for t, v in keys]
            em['sizeOverLife'] = [[rv(t, 3), rv(max(0, v))] for t, v in keys]
        if 'scaleY' in over and 'aspect' not in em:
            log.approx('scale', 'scale y over life -> dropped (no aspect over life)')
        # rotation / spin
        ang = attr.get('angle')
        mesh_out = out is not None and g.kind(out) == 'VFXMeshOutput'
        if ang is not None:
            if mesh_out:
                r3 = {}
                for i, key in enumerate('xy'):
                    c = comp(ang, i)
                    j = rng(vm.arith('Multiply', c, -1.0) if not isinstance(c, Unknown) else c)
                    if j not in (None, 0):
                        r3[key] = j
                if r3:
                    em['rotation3D'] = r3
                jz = rng(comp(ang, 2))
                if jz not in (None, 0):
                    em['rotation'] = jz
            else:
                jz = rng(vm.arith('Multiply', comp(ang, 2), -1.0))
                if jz not in (None, 0):
                    em['rotation'] = jz
        spin = over.get('angle+') or attr.get('angularVelocity')
        if spin is not None:
            z = comp(spin, 2)
            j = rng(z if mesh_out else vm.arith('Multiply', z, -1.0), -36000, 36000)
            if j not in (None, 0):
                em['spin'] = j
            if mesh_out:
                s3 = {}
                for i, key in enumerate('xy'):
                    jj = rng(vm.arith('Multiply', comp(spin, i), -1.0), -36000, 36000)
                    if jj not in (None, 0):
                        s3[key] = jj
                if s3:
                    em['spin3D'] = s3
        # position / velocity
        pos = attr.get('position')
        if pos is not None:
            if isinstance(pos, dict):
                pos = pos.get('position', (0.0, 0.0, 0.0))
            if isinstance(pos, vm.AttrXform):
                self.rotate_shape(em, pos)
                if vm.v_len(pos.add) > 1e-6:
                    em['offset'] = [rv(x + y * self.scale) for x, y in zip(em.get('offset', (0.0, 0.0, 0.0)), to_eff(pos.add))]
                log.ok('position', 'shape rotated (Rotate 3D)')
            elif isinstance(pos, tuple):
                off = tuple(a + b for a, b in zip(to_eff(pos), tuple(em.get('offset', (0, 0, 0)))))
                if vm.v_len(off) > 1e-5:
                    em['offset'] = [rv(x * self.scale) for x in off]
            elif isinstance(pos, Rand) and isinstance(pos.lo, tuple):
                lo, hi = to_eff(pos.lo), to_eff(pos.hi)
                em['shape'] = 'box'
                em['box'] = [rv(abs(b_ - a_) * self.scale) for a_, b_ in zip(lo, hi)]
                em['offset'] = [rv((a_ + b_) * 0.5 * self.scale) for a_, b_ in zip(lo, hi)]
                log.approx('position', 'random position -> box shape')
            else:
                log.drop('position', 'position %r' % (pos,))
        self.velocity(s, em, attr, log)
        if over.get('position+') is not None and isinstance(over['position+'], tuple):
            em['velocity'] = [rv(x * self.scale) for x in to_eff(over['position+'])]
        if over.get('velocity+') is not None and isinstance(over['velocity+'], tuple):
            f = to_eff(over['velocity+'])
            em['force'] = {k: rv(v * self.scale) for k, v in zip('xyz', f) if abs(v) > 1e-6}
            em['force']['space'] = 'world'
        tex_index = attr.get('texIndex')
        # output: texture, blend, flipbook, meshes
        if out is not None:
            self.output(s, out, em, over, tex_index)
        else:
            em['texture'] = 'builtin:soft'
        if strip:
            em['trail'] = {'mode': 'ribbon', 'time': rv(min(10, max(0.01, num(mid(life) if is_num(mid(life)) else 1, 1)))),
                           'inheritColor': True, 'sizeAffectsWidth': True}
            em['hidden'] = True
            log.approx('strip', 'particle strip -> ribbon trail connecting the particles (strip index ignored)')
        if s.subs:
            em['_subs'] = s.subs
        return {k: em[k] for k in sorted(em, key=lambda x: ORDER.index(x) if x in ORDER else 999)}

    def rotate_shape(self, em, xf):
        """p' = M (p - c) + c on the spawn shape: turns shapeRotation and the offset (effect frame)."""
        Me = cv.mm(P, cv.mm(xf.M, PT))
        cur = cv.mat_zxy(em['shapeRotation']) if 'shapeRotation' in em else cv.I3
        M = cv.mm(Me, cur)
        if not cv.is_identity(M):
            em['shapeRotation'] = [rv(x, 3) for x in cv.euler_zxy(M)]
        c = tuple(x * self.scale for x in to_eff(xf.center))
        o = tuple(em.get('offset', (0.0, 0.0, 0.0)))
        r = tuple(a + b for a, b in zip(cv.mv(Me, tuple(x - y for x, y in zip(o, c))), c))
        if vm.v_len(r) > 1e-5:
            em['offset'] = [rv(x) for x in r]
        if em.get('radial') is None and em.get('direction'):
            em['direction'] = [rv(x) for x in cv.mv(Me, tuple(em['direction']))]

    def velocity(self, s, em, attr, log):
        v = vm.unwrap(attr.get('velocity'))
        speed = getattr(s, 'speed', None)
        mode = getattr(s, 'dir_mode', None)
        shape_dir = getattr(s, 'shape_direction', False)
        if speed is not None:
            em['speed'] = rng(speed, -500, 500)
            if mode and mode[0] == 'radial' or (mode and mode[0] == 'direction' and shape_dir and mode[1] < 1):
                em['spread'] = 0
                em['radial'] = rv(min(1, max(-1, mode[1])))
            elif mode and mode[0] == 'direction':
                d = to_eff(mode[2])
                n = vm.v_len(d) or 1
                em['direction'] = [rv(x / n) for x in d]
                em['spread'] = 0
                if shape_dir and mode[1] > 0:
                    em['radial'] = rv(min(1, 1 - mode[1]))
            else:
                em['spread'] = 180
                if mode and mode[1] < 1:
                    em['randomDirection'] = rv(min(1, max(0, mode[1])))
            return
        if v is None:
            em['speed'] = 0
            em['spread'] = 0
            return
        if isinstance(v, tuple):
            d = to_eff(v)
            n = vm.v_len(d)
            em['speed'] = rv(n * self.scale)
            em['spread'] = 0
            if n > 1e-6:
                em['direction'] = [rv(x / n) for x in d]
            return
        if isinstance(v, Rand) and isinstance(v.lo, tuple):
            lo, hi = to_eff(v.lo), to_eff(v.hi)
            m = tuple((a + b) * 0.5 for a, b in zip(lo, hi))
            n = vm.v_len(m)
            corners = [tuple(c) for c in ((lo[0], lo[1], lo[2]), (hi[0], hi[1], hi[2]), (lo[0], hi[1], lo[2]), (hi[0], lo[1], hi[2]))]
            sp = [vm.v_len(c) for c in corners]
            em['speed'] = [rv(min(sp) * self.scale), rv(max(sp) * self.scale)] if max(sp) - min(sp) > 1e-4 else rv(n * self.scale)
            if n > 1e-6:
                em['direction'] = [rv(x / n) for x in m]
                ang = max(math.degrees(math.acos(max(-1, min(1, sum(a * b for a, b in zip(m, c)) / (n * (vm.v_len(c) or 1)))))) for c in corners)
                em['spread'] = rv(min(180, ang), 2)
            else:
                em['spread'] = 180
            log.approx('velocity', 'random velocity box -> cone of directions with a speed range')
            return
        log.drop('velocity', 'velocity %r' % (v,))
        em['speed'] = 0

    def start_color(self, col, alpha, log):
        out = {}
        hdr = False

        def norm(c):
            nonlocal hdr
            c = tuple(c[:3]) + (1.0,) * (3 - len(c[:3]))
            m = max(c)
            if m > 1.0:
                hdr = True
                c = tuple(x / m for x in c)
            return c

        a_lo = a_hi = None
        if is_num(alpha):
            a_lo = a_hi = alpha
        elif isinstance(alpha, Rand) and is_num(alpha.lo):
            a_lo, a_hi = alpha.lo, alpha.hi
        else:
            a_lo = a_hi = 1.0
            if alpha is not None:
                log.approx('color', 'alpha %r -> 1' % (alpha,))
        if isinstance(col, tuple):
            c = norm(col)
            if a_lo == a_hi:
                if c != (1.0, 1.0, 1.0) or a_lo != 1:
                    out['startColor'] = hex_color(c + (a_lo,))
            else:
                out['startColor'] = [hex_color(c + (a_lo,)), hex_color(c + (a_hi,))]
        elif isinstance(col, Rand) and isinstance(col.lo, tuple):
            out['startColor'] = [hex_color(norm(col.lo) + (a_lo,)), hex_color(norm(col.hi) + (a_hi,))]
        elif col is not None:
            log.approx('color', 'colour %r -> white' % (col,))
        if hdr:
            log.approx('color', 'HDR colour (intensity > 1) -> normalized to 1 (use bloom post effect for the glow)')
        return out

    def color_keys(self, over, log):
        keys = None
        if 'color' in over:
            keys = [(t, c) for t, c in over['color'].keys]
            if any(max(c[:3]) > 1 for _, c in keys):
                log.approx('color', 'HDR gradient -> clamped to 1')
        if 'alpha' in over:
            ak = over['alpha']
            if keys is None:
                keys = [(t, (1.0, 1.0, 1.0, v * ak.k)) for t, v in ak.keys]
            else:
                times = sorted(set([t for t, _ in keys] + [t for t, _ in ak.keys]))
                keys = [(t, tuple(_lerp_keys(keys, t)[:3]) + (_lerp_keys(keys, t)[3] * _lerp_vals(ak.keys, t) * ak.k,)) for t in times]
        if keys is None:
            return [[0, '#ffffffff'], [1, '#ffffffff']]
        return [[rv(t, 4), hex_color(c)] for t, c in keys]

    def output(self, s, out, em, over, tex_index):
        g = self.g
        log = s.log
        k = g.kind(out)
        d = out.data
        em['blend'] = BLEND.get(int(num(d.get('blendMode'), 0)), 'additive')
        vals = g.slot_values(out)
        if int(num(d.get('useSoftParticle'), 0)):
            log.drop('output', 'soft particles (no depth reads on weak GPUs)')
        sg = d.get('shaderGraph')
        if isinstance(sg, dict) and get(sg, 'guid'):
            sgp = self.assets.path(sg)
            log.approx('output', 'Shader Graph %s: blend, main texture and colour only (use the engine Shader Graph for the rest)' %
                       (os.path.basename(sgp) if sgp else sg.get('guid')))
        tint = next((v for key, v in vals.items() if key.strip('_').lower() in ('color', 'basecolor', 'tintcolor', 'maincolor')), None)
        if isinstance(tint, Life) and tint.gradient:
            m = max([1.0] + [max(c[:3]) for _, c in tint.keys])
            em['color'] = [[rv(t, 4), hex_color(tuple(x / m for x in c[:3]) + (min(1.0, c[3]),))] for t, c in tint.keys]
            if m > 1:
                log.approx('output', 'HDR colour (intensity %.3g) -> normalized to 1 (bloom gives the glow)' % m)
            log.ok('output', 'shader colour property over life -> colour over life')
        elif isinstance(tint, tuple):
            c = tint + (1.0,) * (4 - len(tint))
            m = max(c[:3])
            c = tuple(x / m for x in c[:3]) + (min(1.0, c[3]),) if m > 1 else tuple(min(1.0, x) for x in c)
            em['color'] = [[t, hex_color(tuple(a * b for a, b in zip(_hexrgba(h), c)))] for t, h in em['color']]
            log.ok('output', 'shader colour property -> tint')
        elif tint is not None:
            log.approx('output', 'shader colour property %r dropped' % (tint,))
        tex = None
        for key in ('mainTexture', 'baseColorMap', 'MainTex_', '_MainTex', 'MainTex', '_BaseMap'):
            if key in vals:
                tex = vals[key]
                break
        if tex is None:
            tex = next((v for key, v in vals.items() if isinstance(v, dict) and isinstance(v.get('obj'), dict)
                        and 'tex' in key.lower() and not any(h in key.lower() for h in ('noise', 'mask', 'normal', 'dissolve'))), None)
        ref = tex.get('obj') if isinstance(tex, dict) else None
        if isinstance(ref, dict) and str(ref.get('guid', '')).lower() != BUILTIN_TEX_GUID and get(ref, 'fileID'):
            path = self.assets.path(ref)
            if path:
                info = MaterialInfo()
                info.texture_path, info.builtin_texture, info.blend = path, False, em['blend']
                em['texture'] = self.tex.get(info)
            else:
                em['texture'] = 'builtin:soft'
                log.approx('output', 'texture %s not found: builtin:soft' % ref.get('guid'))
        else:
            em['texture'] = 'builtin:soft'
        uv = int(num(d.get('uvMode'), 0))
        fb = vals.get('flipBookSize')
        if uv in (1, 2, 4) and isinstance(fb, tuple):
            cols, rows = max(1, int(fb[0])), max(1, int(fb[1]))
            if cols * rows > 1:
                em['frames'] = [cols, rows]
                n = cols * rows
                if 'texIndex' in over:
                    tk = over['texIndex']
                    em['frameOverTime'] = {'curve': [[rv(t, 3), rv(min(1, max(0, v * tk.k / n)))] for t, v in tk.keys]}
                elif over.get('texIndex+') is not None and is_num(over['texIndex+']):
                    em['fps'] = rv(min(120, abs(over['texIndex+'])))
                elif tex_index is not None and rng(tex_index) is not None:
                    em['startFrame'] = rng(tex_index, 0, 255)
                    em['fps'] = 0
                    em['frameOverTime'] = {'curve': [[0, rv(min(1, max(0, num(mid(tex_index), 0) / n)))], [1, rv(min(1, max(0, num(mid(tex_index), 0) / n)))]]}
                else:
                    em['frameOverTime'] = {'curve': [[0, 0], [1, 0]]}
                if uv in (2, 4):
                    log.approx('output', 'flipbook frame blending: frames switch without blending')
        if k == 'VFXMeshOutput':
            mesh = vals.get('mesh')
            ref = mesh.get('obj') if isinstance(mesh, dict) else None
            name, why = self.meshes.get(ref) if isinstance(ref, dict) else (None, 'no mesh')
            if name:
                em['meshes'] = [name]
                if not name.startswith('builtin:'):
                    self.model_turn(em)
            else:
                log.approx('output', 'mesh output without a usable mesh (%s) -> camera billboards' % why)
            if int(num(d.get('MeshCount'), 1)) > 1:
                log.approx('output', 'several meshes: only the first')
        elif k == 'VFXPlanarPrimitiveOutput' and int(num(d.get('primitiveType'), 1)) != 1:
            log.approx('output', '%s primitive -> quad' % ('triangle' if int(num(d.get('primitiveType'), 1)) == 0 else 'octagon'))
        elif k == 'VFXQuadOutput':
            log.approx('output', 'lit / distortion quad output -> unlit quad')
        sp = int(num(d.get('sortingPriority'), 0))
        if sp:
            em['sortOrder'] = sp
        if int(num(d.get('sort'), 0)) == 1:
            em['sort'] = 'distance'
        if int(num(d.get('useAlphaClipping'), 0)) and em['blend'] == 'opaque':
            log.approx('output', 'alpha clipping -> opaque')
        log.ok('output', '%s %s %s' % (k, em['blend'], em.get('texture')))

    def model_turn(self, em):
        """Model files are mirrored in X by Unity's import: R Ry(180) (as the ParticleSystem importer)."""
        r3 = em.setdefault('rotation3D', {})
        r3['y'] = map_range(r3.get('y', 0), lambda x: x + 180)
        r3['x'] = map_range(r3.get('x', 0), lambda x: -x)
        for key in ('rotation', 'spin'):
            if key in em:
                em[key] = map_range(em[key], lambda x: -x)
        if 'spin3D' in em and 'x' in em['spin3D']:
            em['spin3D']['x'] = map_range(em['spin3D']['x'], lambda x: -x)

    fields = []


def _hexrgba(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in range(0, 8, 2))


def map_range(v, f):
    """f over an engine range JSON: number, [min, max] or {"curve": [[t, v]]}."""
    if isinstance(v, list):
        return sorted(rv(f(x)) for x in v)
    if isinstance(v, dict):
        out = dict(v)
        for k in ('curve', 'curveMin'):
            if k in out:
                out[k] = [[t, rv(f(x))] for t, x in out[k]]
        return out
    return rv(f(v))


def _lerp_keys(keys, t):
    if t <= keys[0][0]:
        return keys[0][1]
    for a, b in zip(keys, keys[1:]):
        if t <= b[0]:
            f = (t - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 1
            return tuple(x + (y - x) * f for x, y in zip(a[1], b[1]))
    return keys[-1][1]


def _lerp_vals(keys, t):
    if t <= keys[0][0]:
        return keys[0][1]
    for a, b in zip(keys, keys[1:]):
        if t <= b[0]:
            f = (t - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 1
            return a[1] + (b[1] - a[1]) * f
    return keys[-1][1]


ORDER = ['comment', 'texture', 'frames', 'fps', 'frameOverTime', 'startFrame', 'blend', 'align', 'meshes', 'sort', 'sortOrder', 'hidden',
         'burst', 'bursts', 'rate', 'perMeter', 'delay', 'duration', 'loop', 'life', 'speed', 'size', 'aspect', 'rotation',
         'rotation3D', 'spin', 'spin3D', 'startColor', 'direction', 'spread', 'radial', 'randomDirection', 'shape', 'radius',
         'donutRadius', 'length', 'arc', 'thickness', 'box', 'emitFrom', 'shapeRotation', 'offset', 'space', 'gravity', 'drag',
         'velocity', 'force', 'externalForces', 'noise', 'sizeOverLife', 'color', 'collision', 'trail', 'subEmitters', 'maxParticles']


def convert_file(path, assets, rep, name, game_dir, dry_run=False, scale=1.0, overrides=None, textures=None, meshes=None):
    """One .vfx -> [(effect name, effect json)]: the main effect `name` first, then GPU-event sub-effects."""
    c = Converter(path, assets, rep, name, game_dir, dry_run, scale, overrides, textures, meshes)
    c.fields = []
    outs = c.convert()
    if c.fields and outs:
        outs[0][1]['forceFields'] = c.fields[:8]
    return outs


def visual_effects(prefab_path, assets):
    """VisualEffect components of a prefab: [(asset path, overrides, game object name)]."""
    from unity_yaml import parse_documents
    with open(prefab_path, 'r', encoding='utf-8', errors='replace') as f:
        docs = parse_documents(f.read())
    names = {d.file_id: str(get(d.data, 'm_Name', default='?')) for d in docs if d.class_id == 1}
    out = []
    for d in docs:
        if d.class_id != 2083052967 or d.stripped:
            continue
        path = assets.path(d.data.get('m_Asset') or {})
        out.append((path, vm.property_overrides(d.data.get('m_PropertySheet')), names.get(get(d.data, 'm_GameObject', 'fileID'), '?'),
                    int(num(d.data.get('m_Enabled'), 1))))
    return out


def run(argv=None):
    ap = argparse.ArgumentParser(description='Unity VFX Graph -> AIVERSE fx')
    ap.add_argument('--source', required=True, help='.vfx file, or a prefab with VisualEffect components')
    ap.add_argument('--game', required=True)
    ap.add_argument('--name')
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    opts = ap.parse_args(argv)
    rep = Report()
    src = os.path.abspath(os.path.expanduser(opts.source))
    if not os.path.isfile(src):
        rep.warnings.append('error: no such file: %s' % opts.source)
        return finish(rep, opts, 2)
    assets = Assets(project_roots(src), rep)
    textures = Textures(opts.game, assets, rep, opts.dry_run)
    meshes = Meshes(opts.game, assets, rep, opts.dry_run)
    jobs = []
    stem = sanitize(opts.name or os.path.splitext(os.path.basename(src))[0], 40)
    if src.lower().endswith('.vfx'):
        jobs.append((src, {}, stem))
    else:
        for i, (path, ov, go, on) in enumerate(visual_effects(src, assets)):
            if not path:
                rep.warnings.append('VisualEffect on %s: its .vfx asset is not in the source folder' % go)
                continue
            jobs.append((path, ov, stem if i == 0 else '%s_%d' % (stem, i + 1)))
    total = 0
    for path, ov, name in jobs:
        try:
            outs = convert_file(path, assets, rep, name, opts.game, opts.dry_run, opts.scale, ov, textures, meshes)
        except Exception as e:  # one broken graph must not hide the others
            rep.warnings.append('error: %s: %s: %s' % (os.path.basename(path), type(e).__name__, e))
            continue
        for ename, effect in outs:
            rel = 'fx/%s.json' % ename
            if not opts.dry_run:
                os.makedirs(os.path.join(opts.game, 'fx'), exist_ok=True)
                with open(os.path.join(opts.game, rel), 'w', encoding='utf-8') as f:
                    f.write(json.dumps(effect, indent=1) + '\n')
            rep.effects.append({'name': ename, 'file': rel, 'emitters': len(effect['emitters']), 'source': os.path.basename(path)})
            total += 1
    return finish(rep, opts, 0 if total else 1)


def finish(rep, opts, code):
    print(json.dumps(rep.as_json(), indent=1, ensure_ascii=False) if opts.json else rep.text())
    return code


if __name__ == '__main__':
    sys.exit(run())
