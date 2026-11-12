"""Bunker Wars skills as data: SkillData prefabs (items = elements with conditions, filter, modificators),
skill headers (levels), level prefabs (SkillDataModificator upgrades) applied the way the client does."""
import copy
import os

from unityyaml import num, ref_id

ADD, SET, MUL = 0, 1, 2


def operate(origin, value, op):
    op = int(num(op, SET))
    if op == SET:
        return value
    if op == ADD:
        return origin + value
    if op == MUL:
        return origin * value
    return origin


class Comp:
    """One MonoBehaviour of a prefab: its class and serialized fields (mutable copy)."""
    def __init__(self, fid, cls, fields):
        self.fid, self.cls, self.f = fid, cls, fields

    def __repr__(self):
        return f'<{self.cls} {self.fid}>'


class Element:
    def __init__(self):
        self.spends = False
        self.activate, self.deactivate = [], []   # condition Comps
        self.filter = None                        # filter Comp
        self.mods = []                            # modificator Comps
        self.raw = {}


class Skill:
    def __init__(self):
        self.name = ''
        self.client_id = 0
        self.cooldown = 0.0
        self.initial_cooldown = 0.0
        self.affect_type = 0
        self.bot_skill = 0
        self.elements = []
        self.comps = {}           # fileID -> Comp (for references between components)
        self.source = ''
        self.notes = []


def load_skill(proj, path):
    docs = proj.docs(path)
    s = Skill()
    s.source = path
    for fid, (ucls, body) in docs.items():
        if ucls == 'MonoBehaviour':
            s.comps[fid] = Comp(fid, proj.class_of(body), copy.deepcopy(body))
    main = next((c for c in s.comps.values() if c.cls == 'SkillData'), None)
    if not main:
        return None
    for fid, (ucls, body) in docs.items():
        if ucls == 'GameObject':
            s.name = body.get('m_Name') or os.path.basename(path)[:-7]
    f = main.f
    s.client_id = int(num(f.get('clientId')))
    cd = f.get('cooldown') or {}
    s.cooldown = proj.gd_value(cd.get('key') if isinstance(cd, dict) else None, 0.0)
    s.initial_cooldown = num(f.get('initialCooldown'))
    s.affect_type = int(num(f.get('affectType')))
    s.bot_skill = int(num(f.get('botSkill')))
    for it in f.get('items') or []:
        e = Element()
        e.raw = it
        e.spends = num(it.get('isSpendsEnergy')) != 0
        e.activate = [s.comps[r] for r in map(ref_id, it.get('activateConditionsInfo') or []) if r in s.comps]
        e.deactivate = [s.comps[r] for r in map(ref_id, it.get('deactivateConditionsInfo') or []) if r in s.comps]
        e.filter = s.comps.get(ref_id(it.get('filter')))
        e.mods = [s.comps[r] for r in map(ref_id, it.get('modificators') or []) if r in s.comps]
        s.elements.append(e)
    return s


# ---------------------------------------------------------------------------------------- field paths

def _resolve(proj, skill, comp, path):
    """(container dict, key) for a dotted backing-field path; references to components are followed and
    a component's own properties (startRange -> inRange) are mapped again."""
    parts = path.split('.')
    cur_comp, cur = comp, comp.f
    for i, p in enumerate(parts):
        last = i == len(parts) - 1
        if p in ('Get', 'Get()'):
            return None
        if p not in cur and cur_comp is not None:
            mapped = proj.prop_field(cur_comp.cls, p)
            if mapped and mapped != p:
                rest = '.'.join([mapped] + parts[i + 1:])
                return _resolve(proj, skill, cur_comp, rest) if cur is cur_comp.f else None
        if last:
            return (cur, p)
        nxt = cur.get(p)
        if isinstance(nxt, dict) and 'fileID' in nxt and len(nxt) <= 3 and ref_id(nxt) in skill.comps:
            cur_comp = skill.comps[ref_id(nxt)]
            cur = cur_comp.f
        elif isinstance(nxt, dict):
            if 'key' in nxt and len(nxt) <= 2 and parts[i + 1:i + 2] in (['Get'], ['Get()']):
                return (cur, p)       # GDFloat / GDInt: replaced by a literal
            cur = nxt
            cur_comp = None
        else:
            return None
    return None


def get_value(proj, skill, comp, path, default=0.0):
    r = _resolve(proj, skill, comp, path)
    if not r:
        return default
    d, k = r
    v = d.get(k)
    if isinstance(v, dict) and 'key' in v:
        return proj.gd_value(v.get('key'), default) if '_v' not in v else v['_v']
    if isinstance(v, dict) and 'value' in v:
        return num(v['value'], default)
    return num(v, default) if v is not None else default


def set_value(proj, skill, comp, path, value):
    r = _resolve(proj, skill, comp, path)
    if not r:
        return False
    d, k = r
    v = d.get(k)
    if isinstance(v, dict) and 'key' in v:
        v['_v'] = value
    else:
        d[k] = value
    return True


def field(proj, skill, comp, name, default=0.0):
    """A modificator's numeric field (GD values and FloatValue {value} unwrapped)."""
    return get_value(proj, skill, comp, name, default)


def prop(proj, skill, comp, propname, default=0.0):
    path = proj.prop_field(comp.cls, propname)
    return get_value(proj, skill, comp, path, default) if path else default


def set_prop(proj, skill, comp, propname, fn):
    path = proj.prop_field(comp.cls, propname)
    if not path:
        return False
    return set_value(proj, skill, comp, path, fn(get_value(proj, skill, comp, path)))


# ---------------------------------------------------------------------------------------- upgrades

RANGE_FILTERS = ('SkillRangeFilterBase',)


def _mods(skill):
    for e in skill.elements:
        for m in e.mods:
            yield e, m


def apply_upgrade(proj, skill, up):
    """One SkillDataModificatorBase component, as its Apply(SkillData) does."""
    c, f = up.cls, up.f
    op = f.get('operation', SET)
    val = lambda k: num(f.get(k))

    def each_iface(iface, propnames, fn, conds=False):
        for e in skill.elements:
            targets = list(e.mods) + (list(e.deactivate) if conds else [])
            for m in targets:
                if proj.implements(m.cls, iface):
                    for p in propnames:
                        set_prop(proj, skill, m, p, fn)

    if c == 'SkillCooldown':
        skill.cooldown = operate(skill.cooldown, val('time'), op)
    elif c == 'SkillCooldownInitial':
        skill.initial_cooldown = operate(skill.initial_cooldown, val('time'), op)
    elif c == 'SkillTimeout':
        for e in skill.elements:
            if e.spends:
                for d in e.deactivate:
                    if d.cls == 'SkillModificatorConditionTimeout':
                        d.f['time'] = operate(num(d.f.get('time')), val('time'), op)
    elif c == 'SkillActivationDelay':
        for _, m in _mods(skill):
            cur = num(m.f.get('additionalActivationDelay', -1), -1)
            if int(num(op, SET)) == SET or cur >= 0:
                m.f['additionalActivationDelay'] = operate(cur, val('activationDelay'), op)
    elif c in ('SkillRange', 'SkillRangeTornado'):
        for e in skill.elements:
            if e.filter and proj.implements(e.filter.cls, 'SkillRangeFilterBase'):
                e.filter.f['inRange'] = operate(num(e.filter.f.get('inRange')), val('range'), op)
            if c == 'SkillRangeTornado':
                for m in e.mods:
                    if m.cls == 'UnitsTornado':
                        hf = skill.comps.get(ref_id(m.f.get('houseFilter')))
                        if hf:
                            hf.f['inRange'] = operate(num(hf.f.get('inRange')), val('range'), op)
    elif c == 'SkillRangeVelocity':
        for e in skill.elements:
            if e.filter and e.filter.cls == 'SkillUnitsFilter':
                e.filter.f['rangeVelocity'] = operate(num(e.filter.f.get('rangeVelocity')), val('velocity'), op)
    elif c == 'SkillUnitsCountInFilter':
        for e in skill.elements:
            if e.filter and e.filter.cls == 'SkillUnitsFilter':
                e.filter.f['maxCount'] = round(operate(num(e.filter.f.get('maxCount')), val('count'), op))
    elif c == 'SkillPointsFilterAmount':
        for e in skill.elements:
            if e.filter and e.filter.cls == 'SkillPointsFilter':
                e.filter.f['maxAmount'] = int(val('amount'))
    elif c == 'SkillPointsFilterLevel':
        for e in skill.elements:
            if e.filter and e.filter.cls == 'SkillPointsFilter':
                e.filter.f['levelFrom'], e.filter.f['levelTo'] = int(val('levelFrom')), int(val('levelTo'))
    elif c == 'SkillPointsTypeFilter':
        for e in skill.elements:
            if e.filter and e.filter.cls == 'SkillPointsFilter':
                e.filter.f['typeFilter'] = f.get('typeFilter')
    elif c == 'SkillOwnerFilter':
        for e in skill.elements:
            if e.filter and e.filter.cls in ('SkillPointsFilter', 'SkillUnitsFilter'):
                e.filter.f['ownerFilter'] = f.get('filter')
            elif e.filter and e.filter.cls == 'SkillPlayersFilter':
                e.filter.f['playerFilter'] = f.get('filter')
    elif c == 'SkillModificatorChance':
        for _, m in _mods(skill):
            m.f['chance'] = '1'
            m.f['chanceValue'] = val('chanceValue')
    elif c == 'SkillEffectForce':
        single = int(num(f.get('valueType'))) == 0
        if single:
            each_iface('IModifiableEffectForce', ['effectForce'], lambda o: operate(o, val('force'), op))
        else:
            each_iface('IModifiableEffectForce', ['effectForceFromValue'], lambda o: operate(o, val('minForce'), op))
            each_iface('IModifiableEffectForce', ['effectForceToValue'], lambda o: operate(o, val('maxForce'), op))
    elif c == 'SkillEffectDuration':
        vt = int(num(f.get('valueType')))
        for e in skill.elements:
            for m in list(e.mods) + list(e.deactivate):
                single = proj.implements(m.cls, 'IModifiableEffectDurationSingle')
                rng = proj.implements(m.cls, 'IModifiableEffectDurationRange')
                if not (single or rng):
                    continue
                if vt == 0:
                    if single:
                        set_prop(proj, skill, m, 'effectDuration', lambda o: operate(o, val('time'), op))
                    else:
                        set_prop(proj, skill, m, 'effectDurationFromValue', lambda o: operate(o, val('time'), op))
                        set_prop(proj, skill, m, 'effectDurationToValue', lambda o: operate(o, val('time'), op))
                else:
                    if single:
                        set_prop(proj, skill, m, 'effectDuration', lambda o: operate(o, val('maxTime'), op))
                    if rng or vt == 2:
                        set_prop(proj, skill, m, 'effectDurationFromValue', lambda o: operate(o, val('minTime'), op))
                        set_prop(proj, skill, m, 'effectDurationToValue', lambda o: operate(o, val('maxTime'), op))
    elif c == 'SkillUnitsSpeed':
        each_iface('IModifiableUnitsSpeed', ['unitsEffectSpeed'], lambda o: operate(o, val('speed'), op))
    elif c == 'SkillUnitsSpeedInArea':
        each_iface('IModifiableUnitsSpeedInArea', ['unitsEffectSpeed'], lambda o: operate(o, val('speed'), op))
    elif c == 'SkillUnitsSpeedSecondStage':
        each_iface('IModifiableUnitsSpeedSecondStage', ['effectFirstStageDuration'], lambda o: val('firstStageDuration'))
        each_iface('IModifiableUnitsSpeedSecondStage', ['unitsEffectSpeedSecondStage'], lambda o: operate(o, val('speedSecondStage'), op))
    elif c == 'SkillUnitsCount':
        if int(num(f.get('valueType'))) == 0:
            each_iface('IModifiableUnitsCount', ['effectUnitsCount'], lambda o: operate(o, val('count'), op))
        else:
            each_iface('IModifiableUnitsCount', ['effectUnitsCountFromValue', 'effectUnitsCountToValue'], lambda o: operate(o, val('count'), op))
    elif c == 'SkillChance':
        each_iface('IModifiableEffectChance', ['effectChance'], lambda o: operate(o, val('chance'), op))
    elif c == 'SkillEffectRange':
        each_iface('IModifiableEffectRange', ['effectRange'], lambda o: operate(o, val('range'), op))
    elif c == 'SkillEffectRangeMultiplier':
        each_iface('IModifiableEffectRangeMultiplier', ['effectRangeMultiplier'], lambda o: operate(o, val('range'), op))
    elif c == 'SkillEffectProportion':
        each_iface('IModifiableEffectProportion', ['effectProportion'], lambda o: val('proportion'))
    elif c == 'SkillItemsCount':
        each_iface('IModifiableItemsCount', ['itemsCount'], lambda o: operate(o, val('count'), op))
    elif c == 'SkillItemLevel':
        each_iface('IModifiableItemLevel', ['itemLevel'], lambda o: operate(o, val('level'), op))
    elif c == 'SkillItemTargetLevel':
        each_iface('IModifiableItemTargetLevel', ['itemTargetLevel'], lambda o: operate(o, val('targetLevel'), op))
    elif c in ('SkillDamage', 'SkillAppearDuration'):
        each_iface('IModifiableValue<%s>' % c, ['value'], lambda o: operate(o, val('value'), op))
    elif c == 'SkillWaveFilter':
        for _, m in _mods(skill):
            if proj.implements(m.cls, 'IModifiableWaveFilter'):
                m.f['_waveOwnerFilter'] = f.get('ownerFilter')
    elif c in ('SkillDefences', 'SkillUnitsAmounts', 'SkillRageMods', 'SkillSoulsSpeed', 'SkillEffectProportionList', 'SkillEffectPositions',
               'SkillTowerFireToBuildings', 'SkillUnitSpawnScale', 'SkillHypnoseUnits', 'SkillIgnoreBattleCoefficients', 'SkillPointCastType',
               'SkillCustomFlag', 'SkillCustomOption', 'SkillGroupGiveMorale', 'SkillKillAuraInfinite', 'SkillSuddenlyShortWay',
               'SkillShowAttackingUnitsAlert', 'SkillActivateImmediately', 'SkillAdditionalMoraleStars', 'SkillModOwnerFilter', 'SkillGrassBurn'):
        # list / flag replacements: copied onto every modificator that implements the matching interface
        for _, m in _mods(skill):
            m.f.setdefault('_upgrades', {})[c] = {k: v for k, v in f.items() if not k.startswith('m_')}
    else:
        skill.notes.append(f'upgrade {c}: presentation only (not converted)')


def apply_level(proj, base, level_path):
    """Copy of `base` with the upgrades of a SkillsUpgradeInfo level prefab applied (in the listed order)."""
    sk = copy.deepcopy(base)
    sk.notes = list(base.notes)
    docs = proj.docs(level_path)
    comps = {fid: body for fid, (u, body) in docs.items() if u == 'MonoBehaviour'}
    info = next((b for b in comps.values() if 'mods' in b), None)
    if not info:
        return sk
    for r in info.get('mods') or []:
        b = comps.get(ref_id(r))
        if b is not None:
            apply_upgrade(proj, sk, Comp(ref_id(r), proj.class_of(b), b))
    return sk
