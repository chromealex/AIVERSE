"""Bunker Wars modificator -> AIVERSE effect primitives (docs/bunkerwars-skills.md has the same table).

Every handler gets the modificator (with level upgrades already applied) and a Ctx, and returns a list of
effect objects for rules.json. Ctx.note(fidelity, text) records how close the mapping is:
  exact  - checked against the BW modificator code: same rule (in the engine's fixed-point arithmetic)
  mapped - converted from the serialized fields and the class name, not yet checked line by line against BW
  approx - same intent, simplified (the note says what differs)
  ref    - belongs to another area (flying towers: agent-bw-routes, environment: agent-bw-env)
  none   - presentation only / tutorial bookkeeping: nothing to simulate
"""
from unityyaml import num, ref_id
import skilldata as SD

# PlayersModule.PlayersFilter -> effect filter keys
PLAYERS = {
    0: {'affects': 'all'},                       # All
    1: {'affects': 'self'},                      # Me (player effects) / own objects
    2: {'affects': 'own'},                       # AlliesAndMe
    3: {'affects': 'allies'},                    # Allies
    4: {'affects': 'enemies'},                   # Enemies
    5: {'affects': 'neutral'},                   # Neutral
    6: {'affects': 'enemies', 'neutral': True},  # EnemiesAndNeutral
    7: {'affects': 'all'},                       # AllExceptMe (approximated)
    8: {'affects': 'enemies', 'neutral': True},  # AllExceptAllies
}
# PointInfo.Type bits -> structure tags of templates/bunkerwars (tools/importers/bunkerwars/import.py)
POINT_TYPES = [(0x1, 'bunker'), (0x2, 'tower'), (0x4, 'smithy'), (0x8, 'special1'), (0x10, 'domination'), (0x20, 'kingofthehill'),
               (0x40, 'cave'), (0x80, 'graveyard'), (0x100, 'special2'), (0x200, 'special3'), (0x400, 'special4'), (0x800, 'special5')]


KIND_TYPE = {'bunker': 'bunker', 'tower': 'tower', 'smithy': 'armory'}
SMITHY_ATTACK = [1.5, 1.75, 1.9, 2.0]   # armory attack factor by smithy level (BW BuildingParameters, templates/bunkerwars)


def point_tags(mask):
    m = int(num(mask))
    if m == -1 or m == 0:
        return []
    return [n for bit, n in POINT_TYPES if m & bit]


def owner_keys(v, unit=False):
    k = dict(PLAYERS.get(int(num(v)), {'affects': 'enemies'}))
    k.pop('neutralOnly', None)
    if unit and k.get('affects') == 'self':
        k['affects'] = 'own'
    return k


def fv(d, default=1.0):
    """MathValues.FloatValue {sign, value, ...} -> (sign, value). Sign: 0 Add, 1 Set, 2 Multiply."""
    if isinstance(d, dict):
        return int(num(d.get('sign'), 2)), num(d.get('value'), default)
    return 2, num(d, default)


def stat_mod(stat, d, default=1.0):
    sign, v = fv(d, default)
    if sign == 0:
        return {'stat': stat, 'add': round(v, 4)}
    return {'stat': stat, 'mul': round(v, 4)}   # Set is approximated as a multiplier of the base value


class Ctx:
    def __init__(self, proj, skill, element, base_filter, duration, px):
        self.proj, self.skill, self.el = proj, skill, element
        self.filter = base_filter        # target filter keys from the element's filter (copy before changing)
        self.duration = duration          # element lifetime (deactivate Timeout) in seconds, 0 = instant
        self.px = px                      # BW pixels per cell
        self.notes = []
        self.fidelity = 'mapped'
        self.star = 1.0                   # morale points per BW morale star
        self.el_notes = ''                # element-level notes (filter quirks)
        self.grow = 0.0                   # element units filter: range growth (cells / s, BW rangeVelocity)
        self.aspect = 0.5                 # BW world.aspectRatio: skill ranges are ellipses
        self._verified = False

    def cells(self, v):
        return round(num(v) / self.px, 3)

    def speed(self, v):
        return round(num(v) / self.px, 3)

    def verified(self):
        self._verified = True

    def note(self, fidelity, text):
        order = FIDELITY
        if order.index(fidelity) > order.index(self.fidelity):
            self.fidelity = fidelity
        self.notes.append(text)

    def f(self, m, name, default=0.0):
        return SD.field(self.proj, self.skill, m, name, default)

    def ref(self, m, name):
        return self.skill.comps.get(ref_id(m.f.get(name)))

    def flt(self, **over):
        d = dict(self.filter)
        d.update(over)
        return d

    def status(self, sid, body, duration=None, **filt):
        e = {'type': 'status', 'status': dict({'id': sid}, **body)}
        dur = self.duration if duration is None else duration
        if dur:
            e['duration'] = round(dur, 3)
        e.update(self.flt(**filt))
        return e


H = {}
FIDELITY = ['exact', 'mapped', 'approx', 'ref', 'none', 'missing']
# modificators whose BW code was read and matches the conversion (everything else is "mapped" until checked)
VERIFIED = {'PointsBomb', 'PointsDefenseCoefficient', 'PointsDisable', 'PointsIncreaseLevel', 'PointsFastCapture', 'PointsProductionSpeedCoefficient',
            'PlayersFreezeMorale', 'PlayersAverageMorale', 'PlayersStealSkill', 'UnitsRage', 'UnitsMines', 'UnitsPetrify', 'UnitsJumpingHeads',
            'PointsMagnet', 'PointsLanding', 'PointsKillUnitsPercent', 'PointsResurrection', 'PointsGiveUnits'}


def handler(*names):
    def deco(fn):
        for n in names:
            H[n] = fn
        return fn
    return deco


# ============================================================================================ players

@handler('PlayersAllMorale')
def _(m, c):
    c.note('approx', 'morale set to the cap for the duration (BW restores the saved points afterwards)')
    return [dict({'type': 'resource', 'resource': 'morale', 'amount': 100000}, **c.flt()),
            dict(c.status('bw_full_morale', {'flags': ['freezeRegen'], 'tags': ['buff']}), type='playerStatus', **c.flt())]


@handler('PlayersAverageMorale')
def _(m, c):
    if int(num(m.f.get('averageType'))) == 1:
        c.note('approx', 'averages morale stars as points')
    return [dict({'type': 'equalizeResource', 'resource': 'morale'}, **c.flt())]


@handler('PlayersFreezeMorale')
def _(m, c):
    return [dict(c.status('bw_freeze_morale', {'flags': ['freezeRegen'], 'tags': ['debuff']}), type='playerStatus')]


@handler('PlayersFreezeMana')
def _(m, c):
    return [dict(c.status('bw_freeze_mana', {'flags': ['freezeRegen'], 'tags': ['debuff']}), type='playerStatus')]


@handler('PlayersIncreaseMorale', 'PlayersTemporaryIncreaseMorale')
def _(m, c):
    lv = c.f(m, 'increaseLevel', 1) + c.f(m, 'additionalIncreaseLevel', 0)
    c.note('approx', 'morale stars as resource points (--morale-star-points per star)')
    return [dict({'type': 'resource', 'resource': 'morale', 'amount': round(lv * c.star, 3)}, **c.flt())]


@handler('PlayersOwnerResetSkillStat', 'PlayersShowAmountCaught', 'PlayersNotifyAttacks')
def _(m, c):
    if m.cls == 'PlayersNotifyAttacks':
        return [dict(c.status('bw_alert', {'flags': ['alert'], 'tags': ['buff']}), type='playerStatus', affects='self')]
    c.note('none', 'statistics / HUD only')
    return []


@handler('PlayersSilence')
def _(m, c):
    out = [dict(c.status('bw_silence', {'flags': ['silence'], 'tags': ['debuff', 'magic']}), type='playerStatus')]
    if c.duration:
        out.append(dict({'type': 'cooldown', 'amount': round(c.duration, 3)}, **c.flt()))
    if m.f.get('slotIndexes'):
        c.note('approx', 'silences every slot (BW: only listed slots)')
    return out


@handler('PlayersStealSkill')
def _(m, c):
    return [dict({'type': 'stealSkill', 'duration': round(c.duration, 3) if c.duration else 0}, **c.flt())]


@handler('PlayersCooldownChange')
def _(m, c):
    sign, v = fv(m.f.get('cooldown'), 0)
    if sign != 0:
        c.note('approx', 'cooldown multiplier as seconds')
    return [dict({'type': 'cooldown', 'amount': round(v, 3)}, **c.flt())]


@handler('PlayersDespell', 'UnitsDespell2')
def _(m, c):
    return [{'type': 'dispel', 'shape': 'map', 'affects': 'any'}]


@handler('PlayersLowerCaughtDefense')
def _(m, c):
    sign, v = fv(m.f.get('defenseChange'), 1)
    c.note('approx', 'defense of captured structures lowered for effectTime after each capture: a passive status')
    return [dict(c.status('bw_caught_defense', {'mods': [stat_mod('defense', m.f.get('defenseChange'))], 'tags': ['debuff']}), type='playerStatus')]


# ============================================================================================ structures

def sfilter(c, **over):
    d = c.flt(**over)
    d.setdefault('kind', 'structure')
    return d


@handler('PointsAmulet')
def _(m, c):
    r = c.cells(c.f(m, 'reverseDistance', 100))
    c.note('approx', 'reverses walkers within reverseDistance of the structure (BW: only those heading for it; speed factor kept)')
    tick = [{'type': 'sendBack', 'mode': 'reverse', 'radius': r, 'inTransit': True, **owner_keys(m.f.get('reverseUnitsFilter'), True)}]
    return [c.status('bw_amulet', {'tags': ['buff'], 'tick': {'interval': 0.1, 'effects': tick}}, **sfilter(c))]


@handler('PointsAverageUnits')
def _(m, c):
    c.note('approx', 'per-player amount coefficients after averaging not applied')
    return [dict({'type': 'equalize'}, **sfilter(c))]


@handler('PointsBomb')
def _(m, c):
    pct = c.f(m, 'killPercent', 75) / 100
    out = [dict({'type': 'garrisonKill', 'percent': round(pct, 4)}, **sfilter(c))]
    wf = c.ref(m, 'waveFilter')
    if wf:
        out.append({'type': 'area', 'delay': round(c.f(m, 'waveDelay', 2), 3), 'radius': c.cells(num(wf.f.get('inRange'))),
                    'effects': [dict({'type': 'kill', 'radius': c.cells(num(wf.f.get('inRange'))), 'kind': 'unit'}, **owner_keys(wf.f.get('ownerFilter'), True))]})
    return out


@handler('PointsManyBombs')
def _(m, c):
    pct = c.f(m, 'killPercent', 75) / 100
    out = [{'type': 'projectile', 'arc': True, 'speed': 6, 'homing': True,
            'effects': [{'type': 'garrisonKill', 'percent': round(pct, 4), 'shape': 'target', 'affects': 'any'}]}]
    c.note('approx', 'one bomb per chosen structure, dropped together (BW staggers drops over the duration)')
    return [dict({'type': 'cast', 'effects': out}, **sfilter(c))]


@handler('PointsCastBuilding')
def _(m, c):
    to = point_tags(m.f.get('castType')) if num(m.f.get('castToType'), 1) else []
    lvl = int(c.f(m, 'targetCastLevel', -1))
    keep = round(c.f(m, 'chanceToSaveLevelAfterDecast', 0), 4)
    if not to:   # same kind, other level: a temporary level change
        e = {'type': 'level', 'delta': 1}
        c.note('approx', 'cast to the same kind at targetCastLevel as a +1 level for the duration')
    else:
        e = {'type': 'transform', 'to': 'bw_' + to[0] + (str(lvl) if lvl > 0 else ''), 'keepChance': keep}
        c.note('approx', 'transform to the type "bw_<kind><level>" of the bunker wars data importer')
    if c.duration:
        e['duration'] = round(c.duration, 3)
    return [dict(e, **sfilter(c, affects=c.filter.get('affects', 'any')))]


@handler('PointsCloseRows')
def _(m, c):
    c.note('approx', 'BW closes rows of the next group sent from the structure; here: own groups in transit form up and speed up')
    return [{'type': 'formation', 'shape': 'map', 'inTransit': True, 'affects': 'own'},
            c.status('bw_close_rows', {'tags': ['buff'], 'mods': [{'stat': 'speed', 'mul': round(c.f(m, 'groupSpeedCoefficient', 1), 4)}]},
                     shape='map', inTransit=True, affects='own', kind='unit')]


@handler('PointsDefector', 'PointsDefector2')
def _(m, c):
    if m.cls == 'PointsDefector':
        return [dict({'type': 'garrisonConvert', 'percent': round(c.f(m, 'defectorsPercent', 20) / 100, 4)}, **sfilter(c))]
    c.note('approx', 'production of the structure walks to the caster: redirect (sendUnitsPerSecond for towers/smithies ignored)')
    e = {'type': 'redirect'}
    if c.duration:
        e['duration'] = round(c.duration, 3)
    return [dict(e, **sfilter(c))]


@handler('PointsDefenseCoefficient', 'PointsSwap', 'PointsMagnet')
def _defense(m, c):
    out = []
    if m.cls == 'PointsDefenseCoefficient':
        out.append(c.status('bw_defense', {'tags': ['buff'], 'mods': [stat_mod('defense', m.f.get('value'))]}, **sfilter(c)))
    if m.cls == 'PointsSwap':
        # PointsSwap.Swap: points[0] exchanges value, building (type + level) and owner with a random point of
        # exchangeType owned per exchangeOwner (relative to the caster); 0.5 s later each gets the first defence
        # entry whose playersFilter matches its new owner (personal defence, until the element ends)
        withk = dict(owner_keys(m.f.get('exchangeOwner', 1)))
        tags = point_tags(m.f.get('exchangeType', 0x7))
        if tags:
            withk['tags'] = tags
        sw = dict({'type': 'swap', 'with': withk, 'swapType': True}, **sfilter(c))
        mods = (m.f.get('_upgrades') or {}).get('SkillDefences', {}).get('defenceMods') or m.f.get('pointDefenceCoefficients') or []
        then = []
        for dm in mods if isinstance(mods, list) else []:
            st = {'type': 'status', 'status': {'id': 'bw_swap_defense', 'tags': ['buff'], 'mods': [stat_mod('defense', dm.get('defenceMod'))]},
                  'shape': 'target', 'kind': 'structure', **owner_keys(dm.get('playersFilter'), True)}
            if c.duration:
                st['duration'] = round(max(0.0, c.duration - 0.5), 3)
            then.append(st)
        if then:
            sw['effects'] = [{'type': 'area', 'delay': 0.5, 'goal': 'target', 'speed': 1000, 'radius': 0.5, 'onArrive': then}]
        c.note('approx', 'skills of the swapped points with deactivateOnSwap are not cancelled; lastCatchTick not swapped')
        return [sw]
    if m.cls == 'PointsMagnet':
        md = m.f.get('magnetData') or {}
        uf = c.ref(m, 'unitsFilter')
        pull = {'type': 'pull', 'radius': c.cells(num(uf.f.get('inRange')) if uf else 300), 'inTransit': True, 'kind': 'unit', 'goal': 'target',
                'speed': c.speed(md.get('startVelocity', 0)), 'accel': c.speed(md.get('acceleration', 500))}
        if num(md.get('minVelocity'), -1) > 0:
            pull['minSpeed'] = c.speed(md.get('minVelocity'))
        if num(md.get('maxVelocity'), -1) > 0:
            pull['maxSpeed'] = c.speed(md.get('maxVelocity'))
        pull.update(owner_keys(uf.f.get('ownerFilter') if uf else 0, True))
        out.append(c.status('bw_magnet', {'tags': ['buff'], 'tick': {'interval': 0.05, 'effects': [pull]}}, **sfilter(c)))
    mods = (m.f.get('_upgrades') or {}).get('SkillDefences', {}).get('defenceMods') or m.f.get('pointDefenceCoefficients') or []
    for dm in mods if isinstance(mods, list) and m.cls != 'PointsSwap' else []:
        out.append(c.status('bw_defense_mod', {'tags': ['buff'], 'mods': [stat_mod('defense', dm.get('defenceMod'))]},
                            **sfilter(c, **owner_keys(dm.get('playersFilter')))))
    return out


@handler('PointsDisable', 'PointsStopTower')
def _(m, c):
    return [c.status('bw_disabled', {'flags': ['lock', 'disarm'], 'tags': ['debuff', 'magic']}, **sfilter(c))]


@handler('PointsDisableForPlayer')
def _(m, c):
    return [dict(c.status('bw_disable_player', {'flags': ['lock'], 'tags': ['debuff']}), type='playerStatus')]


@handler('PointsEnhanceWeapon')
def _(m, c):
    lv = c.f(m, 'additionalWeapon', 0)
    c.note('approx', 'extra smithy (weapon) levels of produced units as a damage bonus per level')
    return [c.status('bw_enhance_weapon', {'tags': ['buff'], 'mods': [{'stat': 'damage', 'add': round(lv, 3)}]}, **sfilter(c))]


@handler('PointsFastCapture')
def _(m, c):
    keep = c.f(m, 'percentUnitToSave', 0.5)
    return [dict({'type': 'garrisonKill', 'percent': round(1 - keep, 4)}, **sfilter(c)), dict({'type': 'capture'}, **sfilter(c))]


@handler('PointsFastConvert')
def _(m, c):
    c.note('approx', 'upgrade / conversion cost and time multipliers; chanceToUpgrade of neighbours not converted')
    cost = c.f(m, 'modifyBuildingCost', 0)
    return [c.status('bw_fast_convert', {'tags': ['buff'], 'mods': [{'stat': 'upgradeCost', 'mul': 0 if cost <= 0 else 0.5}]},
                     **sfilter(c, affects='own'))]


@handler('PointsHypnosis')
def _(m, c):
    c.note('approx', 'the caster commands the structure for the duration (temporary capture)')
    e = {'type': 'capture'}
    if c.duration:
        e['duration'] = round(c.duration, 3)
    return [dict(e, **sfilter(c))]


@handler('PointsIncreaseLevel', 'PointsUpgrade')
def _(m, c):
    d = int(c.f(m, 'upgradeValue', 1)) if m.cls == 'PointsIncreaseLevel' else 1
    return [dict({'type': 'level', 'delta': d}, **sfilter(c))]


@handler('PointsInvulnerability')
def _(m, c):
    # PointsInvulnerability: with unitsPerSecond > 0 each point pays 1 unit at once and 1 every 1 / unitsPerSecond s,
    # and loses it when it has no unit left to pay; otherwise it lasts `timeout` s (the element's own conditions also end it)
    ups = c.f(m, 'unitsPerSecond', 6)
    body = {'flags': ['invulnerable'], 'tags': ['buff', 'magic']}
    c.verified()
    if ups > 0:
        body.update(endWhenEmpty=True, tick={'interval': round(1 / ups, 4), 'effects': [{'type': 'garrisonKill', 'amount': 1, 'shape': 'target', 'affects': 'any'}]})
        return [dict({'type': 'garrisonKill', 'amount': 1}, **sfilter(c)), c.status('bw_invulnerable', body, **sfilter(c))]
    t = c.f(m, 'timeout', 0)
    dur = min(t, c.duration) if (t > 0 and c.duration) else (t or c.duration)
    return [c.status('bw_invulnerable', body, duration=dur, **sfilter(c))]


@handler('PointsKillLeavingUnits')
def _(m, c):
    c.note('approx', 'units leaving the structure die near it (BW: after delayToKill seconds, at most maximumUnitsToKill)')
    tick = [{'type': 'kill', 'radius': 1.5, 'inTransit': True, 'kind': 'unit', 'affects': 'all', 'maxTargets': 2}]
    return [c.status('bw_kill_leaving', {'tags': ['debuff'], 'tick': {'interval': 0.5, 'effects': tick}}, **sfilter(c))]


@handler('PointsKillUnitsPercent', 'PointsKillContinuous')
def _(m, c):
    if m.cls == 'PointsKillUnitsPercent':
        return [dict({'type': 'garrisonKill', 'percent': round(c.f(m, 'destroyPartOfUnits'), 4)}, **sfilter(c))]
    per = c.f(m, 'percentDestroyTotal', 0.1)
    return [c.status('bw_kill_continuous', {'tags': ['debuff'], 'tick': {'interval': 1, 'effects': [
        {'type': 'garrisonKill', 'percent': round(per, 4), 'shape': 'target', 'affects': 'any'}]}}, **sfilter(c))]


@handler('PointsLanding', 'PointsGiveUnits', 'PointsReplenishUnits', 'PointsRecoverUnits', 'PointsResurrection')
def _(m, c):
    if m.cls == 'PointsLanding':
        ups = c.f(m, 'addUnitsPerSecond', 10)
        if int(num(m.f.get('limitType'))) != 0:
            c.note('approx', 'limitType caps not applied')
        return [c.status('bw_landing', {'tags': ['buff'], 'garrisonPerSecond': round(ups, 4)}, **sfilter(c))]
    if m.cls == 'PointsGiveUnits':
        return [dict({'type': 'garrisonAdd', 'amount': round(c.f(m, 'giveUnits', 30), 3)}, **sfilter(c))]
    if m.cls == 'PointsResurrection':
        return [dict({'type': 'garrisonAdd', 'percent': round(c.f(m, 'restorePercent', 50) / 100, 4), 'ofLost': True}, **sfilter(c))]
    c.note('approx', 'refill toward a share of the maximum as a percent of the garrison')
    return [dict({'type': 'garrisonAdd', 'percent': round(c.f(m, 'percentToReplenish' if m.cls == 'PointsReplenishUnits' else 'recoverPercentTarget', 0.5), 4)},
                 **sfilter(c))]


@handler('PointsMoraleThief')
def _(m, c):
    c.note('approx', 'steals morale stars as resource points from the owner of the structure')
    return [dict({'type': 'resource', 'resource': 'morale', 'amount': -round(c.f(m, 'starStealAmount', 1) * c.star, 3), 'transfer': True},
                 **sfilter(c))]


@handler('PointsPanic')
def _(m, c):
    pct = c.f(m, 'panicPercent', 100) / 100
    if c.f(m, 'chanceToNeutral', 0) > 0:
        c.note('approx', 'chanceToNeutral (panicking units turn into skeletons) not converted')
    return [dict({'type': 'garrisonPanic', 'percent': round(pct, 4)}, **sfilter(c))]


@handler('PointsPlague')
def _(m, c):
    st = {'tags': ['debuff', 'magic'], 'flags': ['spreads'], 'garrisonPerSecond': -round(c.f(m, 'decreaseUnitsPerSecond', 2), 4)}
    chance = c.f(m, 'infectionChancePercent', 100)
    e = c.status('bw_plague', st, **sfilter(c))
    if chance < 100:
        c.note('approx', 'infection chance of the carried plague not converted')
    return [e]


@handler('PointsProductionSpeedCoefficient', 'PointsIncreaseProduction')
def _(m, c):
    if m.cls == 'PointsIncreaseProduction':
        return [c.status('bw_production', {'tags': ['buff'], 'mods': [{'stat': 'productionSpeed', 'mul': 2}]}, **sfilter(c))]
    v = m.f.get('value') or {}
    sign = int(num(v.get('sign'), 2)) if isinstance(v, dict) else 2
    val = num(v.get('value'), 1) if isinstance(v, dict) else 1
    mod = {'stat': 'productionSpeed', 'add': val} if sign == 0 else {'stat': 'productionSpeed', 'mul': val}
    return [c.status('bw_production', {'tags': ['buff' if val >= 1 else 'debuff'], 'mods': [mod]}, **sfilter(c))]


@handler('PointsProductionLimit')
def _(m, c):
    c.note('approx', 'tutorial production cap: production stops for the duration')
    return [c.status('bw_production_limit', {'tags': ['debuff'], 'mods': [{'stat': 'productionSpeed', 'mul': 0}]}, **sfilter(c))]


@handler('PointsRedirectProduction')
def _(m, c):
    # PointsRedirectProduction: the owner's other houses send their production delta to points[0] (accumulatedUnits,
    # added on its ReceiveUnits) and grow addUnitsPerSecond instead while it lasts; a newer redirect takes the pool over
    st = {'tags': ['buff'], 'collect': {'addPerSecond': round(c.f(m, 'addUnitsPerSecond', 0), 4)}}
    c.verified()
    return [c.status('bw_redirect', st, **sfilter(c))]


@handler('PointsSabotage')
def _(m, c):
    n = c.f(m, 'unitsCount', 0)
    c.note('approx', 'BW sends unitsCount neutral attackers spread over the structures (ignoring morale / smithies / shields): direct garrison loss')
    return [dict({'type': 'garrisonKill', 'amount': round(n, 3)}, **sfilter(c))]


@handler('PointsSkeletonArmy', 'PointsGhostArmy')
def _(m, c):
    n = int(c.f(m, 'armyCount' if m.cls == 'PointsSkeletonArmy' else 'unitsCount', 10))
    c.note('approx', 'army spawns next to the structure as "bw_skeleton" units of the caster and assaults it')
    return [{'type': 'spawn', 'unit': 'bw_skeleton', 'count': n}]


@handler('PointsSpy', 'PointsShowAmount', 'UnitsExploring')
def _(m, c):
    # PointsSpy.Use: ShowHideValue on the points until deactivated; UnitsExploring.ScanUnits: the filtered units get
    # showSos (or showTargetAmount) for the HUD of the point they attack - presentation flags only
    if m.cls == 'UnitsExploring':
        c.verified()
        return [c.status('bw_exploring', {'flags': ['alert'], 'tags': ['buff']}, **ufilter(c))]
    c.verified()
    return [c.status('bw_spy', {'flags': ['revealed'], 'tags': ['debuff']}, **sfilter(c))]


@handler('PointsSuppressDefense')
def _(m, c):
    # PointsSuppressDefense: suppressDefenseCoefficient += s while active; defence = Lerp(totalDef, 1, sum)
    s = c.f(m, 'supressDefenseCoefficient', 0)
    c.verified()
    return [c.status('bw_suppress', {'tags': ['debuff'], 'stacking': 'independent', 'mods': [{'stat': 'defense', 'lerp': round(s, 4)}]}, **sfilter(c))]


@handler('PointsTeleport')
def _(m, c):
    pct = c.f(m, 'unitsTeleportPercent', 50) / 100
    c.note('approx', 'garrison units moved from the chosen structure to the caster\'s nearest structure (BW digs them along a route)')
    return [dict({'type': 'garrisonConvert', 'percent': round(pct, 4)}, **sfilter(c))]


@handler('PointsBarrier')
def _(m, c):
    c.note('approx', 'wall across the lanes at the click: a "bw_barrier" unit with blocksLanes and hp = unitsCount')
    return [{'type': 'spawn', 'unit': 'bw_barrier', 'duration': round(c.duration or 0, 3)}]


@handler('PointsUnitsKill')
def _(m, c):
    return H['UnitsKill'](m, c)


@handler('PointsMakeStonedUnits')
def _(m, c):
    uf = c.ref(m, 'unitsFilter')
    d = (c.f(m, 'minStonedDuration', 3) + c.f(m, 'maxStonedDuration', 5)) / 2
    r = c.cells(num(uf.f.get('inRange'))) if uf else 3
    return [{'type': 'area', 'delay': round(c.f(m, 'waveDelay', 0), 3), 'radius': r,
             'effects': [{'type': 'status', 'status': {'id': 'bw_stoned', 'flags': ['stun'], 'tags': ['debuff']}, 'duration': round(d, 3), 'radius': r,
                          'kind': 'unit', **owner_keys(uf.f.get('ownerFilter') if uf else 4, True)}]}]


@handler('PointsWarChains', 'PointsDeadLock')
def _(m, c):
    return [c.status('bw_chains', {'flags': ['lock'], 'tags': ['debuff']}, **sfilter(c))]


@handler('PointsRevenge')
def _(m, c):
    return [c.status('bw_revenge', {'tags': ['buff'], 'onCaptured': [{'type': 'garrisonKill', 'percent': 1, 'shape': 'target', 'affects': 'any'}]},
                     **sfilter(c))]


@handler('PointsShareUnits')
def _(m, c):
    e = {'type': 'link'}
    if c.duration:
        e['duration'] = round(c.duration, 3)
    return [dict(e, **sfilter(c))]


@handler('PointsChargeTower', 'PointsReaver')
def _(m, c):
    c.note('approx', 'tower weapon range / damage up for the duration')
    return [c.status('bw_charge_tower', {'tags': ['buff'], 'mods': [{'stat': 'range', 'mul': round(c.f(m, 'rangeCoefficient', 1), 4)}]}, **sfilter(c))]


@handler('PointsSpeedArea', 'UnitsSpeedInAreaCoefficient')
def _(m, c):
    k = c.f(m, 'speedCoefficient', 1)
    r = c.filter.get('radius', 3)
    return [{'type': 'area', 'trigger': 'periodic', 'interval': 0.25, 'lifetime': round(c.duration or 10, 3), 'radius': r,
             'effects': [{'type': 'status', 'status': {'id': 'bw_speed_area', 'mods': [{'stat': 'speed', 'mul': round(k, 4)}]}, 'duration': 0.3,
                          'radius': r, 'kind': 'unit', 'affects': 'all'}]}]


@handler('PointsSoulsCollector', 'PointsEvilSouls', 'PointsSpiritedAway')
def _(m, c):
    r = c.cells(c.f(m, 'deathRadius', 200))
    n = int(c.f(m, 'maxUnits' if m.cls == 'PointsSoulsCollector' else 'count', 100))
    if m.cls != 'PointsSoulsCollector':
        c.note('approx', 'souls variants converted as a souls collector')
    return [dict({'type': 'area', 'motion': 'follow', 'radius': r, 'lifetime': round(c.duration or 30, 3), 'trigger': 'periodic', 'interval': 1000,
                  'maxTriggers': n, 'affects': 'any',
                  'onDeath': [{'type': 'projectile', 'start': 'point', 'goal': 'target', 'speed': c.speed(150),
                               'effects': [{'type': 'garrisonAdd', 'amount': 1, 'affects': 'own'}]}]}, **sfilter(c))]


@handler('PointsWorm')
def _(m, c):
    md = m.f.get('magnetData') or {}
    uf = c.ref(m, 'unitsFilter')
    c.note('approx', 'worm: pulls walkers into the structure, then eats a share of its garrison')
    pull = {'type': 'pull', 'radius': c.cells(num(uf.f.get('inRange')) if uf else 300), 'inTransit': True, 'kind': 'unit', 'goal': 'target', 'affects': 'all',
            'speed': c.speed(md.get('startVelocity', 0)), 'accel': c.speed(md.get('acceleration', 500))}
    return [c.status('bw_worm', {'tags': ['debuff'], 'tick': {'interval': 0.05, 'effects': [pull]},
                                 'onExpire': [{'type': 'garrisonKill', 'percent': round(c.f(m, 'percentToKillInsidePoint', 0.5), 4), 'shape': 'target', 'affects': 'any'}]},
                     **sfilter(c))]


@handler('PointsVictim', 'UnitsSacrifice')
def _(m, c):
    per = c.f(m, 'moralePerUnit' if m.cls == 'PointsVictim' else 'moralePointsPerUnits', 1)
    if m.cls == 'PointsVictim':
        return [dict({'type': 'sacrifice', 'percent': 1, 'gain': {'morale': round(per, 4)}}, **sfilter(c))]
    # UnitsSacrifice: the filtered units carry sacrificerSkill; when one is killed (Unit.Kill) or assaults a point
    # (BaseModule.Attack) its owner gains moralePointsPerUnits per unit
    c.note('approx', 'BW towers also skip their usual morale transfer for these units')
    return [c.status('bw_sacrifice', {'tags': ['buff'], 'deathGain': {'morale': round(per, 4)}}, duration=0, **ufilter(c))]


@handler('PointsEndGameOnCatch', 'PointsHauntedHouse', 'PointsChickenCoop', 'PointsIllusionUnits', 'PointsFlyingUnits', 'PointsShortWay',
         'PointsMapWind', 'PointsPossession', 'PointsDefenseCalculation', 'PointsRecoverUnitsVisual', 'HeroSpecial1')
def _misc(m, c):
    k = m.cls
    if k == 'PointsRecoverUnitsVisual':
        c.note('none', 'presentation of PointsRecoverUnits')
        return []
    if k == 'PointsPossession':
        e = {'type': 'capture'}
        if c.duration:
            e['duration'] = round(c.duration, 3)
        return [dict(e, **sfilter(c))]
    if k == 'PointsFlyingUnits':
        return [c.status('bw_flying', {'flags': ['flying'], 'tags': ['buff'], 'mods': [{'stat': 'speed', 'mul': round(c.f(m, 'speedCoefficient', 1.2), 4)}]},
                         shape='map', inTransit=True, affects='own', kind='unit')]
    if k == 'PointsIllusionUnits':
        return [{'type': 'clone', 'fake': True, 'shape': 'map', 'inTransit': True, 'affects': 'own'}]
    if k == 'PointsShortWay':
        c.note('approx', 'portal at the click; the exit (BW: next to the chosen structure) is not resolved: units stay')
        return [{'type': 'area', 'trigger': 'contact', 'radius': c.cells(c.f(m, 'distance', 50)), 'lifetime': round(c.duration or 10, 3),
                 'maxTriggers': int(c.f(m, 'count', 30)), 'affects': 'own', 'teleportTo': [0, 0]}]
    if k == 'PointsMapWind':
        return [{'type': 'playerStatus', 'status': {'id': 'bw_wind', 'mods': [{'stat': 'speed', 'mul': 0.8}]}, 'duration': round(c.duration or 10, 3), 'affects': 'any'}]
    if k == 'PointsDefenseCalculation':
        c.note('none', 'defense recalculation bookkeeping')
        return []
    c.note('missing', f'{k}: no generic primitive yet')
    return []


@handler('PointsTutorialLimitMoveFrom', 'PointsTutorialWaitCaught', 'PointsTutorialWaitConvertFinished', 'PointsTutorialWaitConvertStarted',
         'PointsTutorialWaitTowerFire', 'PointsTutorialWaitUpgradeFinished', 'PointsTutorialWaitUpgradeStarted', 'PositionPlaceholder')
def _(m, c):
    c.note('none', 'tutorial / placeholder (tutorial agent: triggers)')
    return []


# ============================================================================================ map positions

def ref_filter(c, flt):
    """Filter keys of a filter referenced by a modificator field (same rules as the element filter)."""
    import convert as CV   # late import: convert imports this module
    notes = []
    keys, _ = CV.filter_keys(c.proj, c.skill, flt, c.px, notes)
    return keys


@handler('PositionBlindTowers')
def _(m, c):
    # PositionBlindTowers.Use: points of blindBuildingFilter get blindCounter (TowerModule.isBlind: no shooting) until the
    # element ends; AdvanceTick.TestUnits: every unit inside the element's units filter is isInBlindSpot (towers skip it,
    # TowerModule.CanAttack) and, for applySpeedTo relative to the caster, blindSpotSpeedCoefficient (reset every tick)
    bf = c.ref(m, 'blindBuildingFilter')
    bk = ref_filter(c, bf) if bf else {'kind': 'structure', 'tags': ['tower'], 'shape': 'map', 'affects': 'enemies'}
    out = [{'type': 'status', 'status': {'id': 'bw_blind', 'flags': ['blind'], 'tags': ['debuff']}, **bk}]
    if c.duration:
        out[0]['duration'] = round(c.duration, 3)
    k = c.f(m, 'unitsSpeedCoefficient', 0.7)
    spot = [{'type': 'status', 'status': {'id': 'bw_blind_spot', 'flags': ['untargetable']}, 'duration': 0.1, **ufilter(c, affects='any')}]
    if k != 1:
        spot.append({'type': 'status', 'status': {'id': 'bw_blind_speed', 'mods': [{'stat': 'speed', 'mul': round(k, 4)}]}, 'duration': 0.1,
                     **ufilter(c, **owner_keys(m.f.get('applySpeedTo', 6), True))})
    if c.duration:
        out.append({'type': 'area', 'trigger': 'periodic', 'interval': 0.05, 'lifetime': round(c.duration, 3),
                    'radius': c.filter.get('radius', 1), 'effects': spot})
    c.verified()
    return out


@handler('PositionFlyingTower')
def _(m, c):
    c.note('ref', 'flying tower: agent-bw-routes owns the mobile garrison; spawned as "bw_flying_tower"')
    return [{'type': 'spawn', 'unit': 'bw_flying_tower', 'duration': round(c.duration or 0, 3)}]


@handler('PositionMines')
def _(m, c):
    n = int(c.f(m, 'minesCount', 10))
    return [{'type': 'area', 'trigger': 'contact', 'maxTriggers': 1, 'radius': c.cells(c.f(m, 'detectionRange', 30)), 'lifetime': round(c.f(m, 'timeout', 20), 3),
             'count': n, 'effects': [{'type': 'kill', 'radius': c.cells(c.f(m, 'explosionRange', 80)), 'kind': 'unit'}]}]


# ============================================================================================ units

def ufilter(c, **over):
    d = c.flt(**over)
    d.setdefault('kind', 'unit')
    return d


@handler('UnitsAttack')
def _(m, c):
    c.note('approx', 'walkers get a weapon range bonus for the duration (BW gives units in transit a temporary attack)')
    return [c.status('bw_units_attack', {'tags': ['buff'], 'mods': [{'stat': 'range', 'add': c.cells(c.f(m, 'fireRange', 100))}]},
                     duration=c.f(m, 'duration', 5), **ufilter(c))]


@handler('UnitsAttackCoefficient', 'UnitsDisarm')
def _(m, c):
    if m.cls == 'UnitsDisarm':
        return [c.status('bw_disarm', {'tags': ['debuff'], 'mods': [{'stat': 'damage', 'mul': round(c.f(m, 'attackCoefficient', 0.5), 4)}]}, **ufilter(c))]
    return [c.status('bw_attack', {'tags': ['buff'], 'mods': [stat_mod('damage', m.f.get('value'))]}, **ufilter(c))]


@handler('UnitsBetrayalGarden', 'UnitsBetrayalGarden2')
def _(m, c):
    # UnitsBetrayalGarden2.Use: the first unitsMaxCount units of the filter join a caster group on the same route with
    # speedCoefficient / attackCoefficient applied for good; each of the other filtered units dies with chanceToKill
    f = ufilter(c)
    if m.cls != 'UnitsBetrayalGarden2':
        return [dict({'type': 'convert'}, **f)]
    n = int(c.f(m, 'unitsMaxCount', 0))
    fmax = f.get('maxTargets')
    conv = dict(f, type='convert', maxTargets=min(max(n, 0), fmax) if fmax else max(n, 0))
    mods = [x for x in (stat_mod('speed', m.f.get('speedCoefficient')), stat_mod('damage', m.f.get('attackCoefficient')))
            if x.get('mul', 1) != 1 or x.get('add', 0) != 0]
    if mods:
        conv['effects'] = [{'type': 'status', 'status': {'id': 'bw_betrayal', 'tags': ['buff'], 'mods': mods}, 'shape': 'target',
                            'kind': 'unit', 'affects': 'own'}]
    out = [conv] if n > 0 else []
    ck = c.f(m, 'chanceToKill', 0)
    if ck > 0:
        kill = dict({k: v for k, v in f.items() if k != 'maxTargets'}, type='kill', targetChance=round(ck, 4))
        if fmax:
            kill['maxTargets'] = max(0, fmax - n)
        if not fmax or fmax > n:
            out.append(kill)
    c.verified()
    return out


@handler('UnitsBlizzardRain')
def _(m, c):
    # UnitsBlizzardRain: bulletsCount shots, one every (timeout - dropTime) / (bulletsCount + 2) s from the cast, at the
    # explosionPositions (shuffled once, cycled) x (skillRange - effectRange) around the click (y x aspectRatio); each
    # lands after dropTime: points of pointsFilter lose pointKillPerBullet units, units of unitsFilter die
    n = int(c.f(m, 'bulletsCount', 0))
    drop = c.f(m, 'dropTime', 0)
    per = c.f(m, 'pointKillPerBullet', 0)
    uf, pf = c.ref(m, 'unitsFilter'), c.ref(m, 'pointsFilter')
    eff_range = num(pf.f.get('inRange')) if pf else 0
    skill_range = c.filter.get('radius', 0) * c.px
    rx = (skill_range - eff_range) / c.px
    ry = rx * c.aspect
    pos = [p for p in (m.f.get('explosionPositions') or []) if isinstance(p, dict)]
    if not pos or n <= 0:
        return []
    offsets = [[round(num(p.get('x')) * rx, 3), round(-num(p.get('y')) * ry, 3)] for p in pos]
    hit = []
    if pf:
        hit.append(dict(ref_filter(c, pf), type='garrisonKill', amount=round(per, 3)))
    if uf:
        hit.append(dict(ref_filter(c, uf), type='kill'))
    cool = max(0.0, (c.duration - drop) / (n + 2)) if c.duration else 0
    bomb = {'type': 'area', 'delay': round(drop, 3), 'radius': 0.5, 'effects': hit} if drop > 0 else None
    cast = {'type': 'cast', 'offsets': offsets, 'shuffle': True, 'shots': n, 'stagger': round(cool, 4),
            'effects': [bomb] if bomb else hit}
    c.verified()
    return [cast]


@handler('UnitsChainLightning')
def _(m, c):
    n = int(c.f(m, 'randomUnitsCount', 150))
    props = m.f.get('buildingsLoseUnitsCoefficient') or []
    out = [{'type': 'kill', 'shape': 'map', 'kind': 'unit', 'sort': 'random', 'maxTargets': n, 'affects': c.filter.get('affects', 'enemies')}]
    if props:
        out.append({'type': 'chain', 'jumps': max(0, len(props) - 1), 'jumpRadius': 12, 'kind': 'structure',
                    'jumpFalloff': round(num(props[1]) / max(num(props[0]), 1e-6), 4) if len(props) > 1 else 1,
                    'effects': [{'type': 'garrisonKill', 'percent': round(num(props[0]), 4), 'shape': 'target'}]})
    return out


@handler('UnitsFreeze', 'PointsFreeze', 'PointsStoneUnits', 'UnitsPetrifyStone')
def _(m, c):
    lo, hi = c.f(m, 'minFreezeDuration', 0), c.f(m, 'maxFreezeDuration', 0)
    d = c.f(m, 'freezeDuration', 0) or (lo + hi) / 2 or c.duration
    if hi > lo:
        c.note('approx', f'freeze duration {lo}..{hi} s as the mean')
    f = sfilter(c) if m.cls == 'PointsFreeze' else ufilter(c)
    return [c.status('bw_frozen', {'flags': ['stun'], 'tags': ['debuff', 'magic']}, duration=d, **f)]


@handler('PointsFreezeWave')
def _(m, c):
    lo, hi = c.f(m, 'minFreezeDuration', 10), c.f(m, 'maxFreezeDuration', 14)
    ff = c.ref(m, 'freezeFilter')
    r = c.cells(num(ff.f.get('inRange'))) if ff else 1
    return [{'type': 'area', 'motion': 'lane', 'speed': c.speed(c.f(m, 'waveVelocity', 200)), 'trigger': 'periodic',
             'interval': round(max(0.05, c.f(m, 'checkFreezeTimeout', 0.1)), 3), 'radius': r, 'lifetime': 30,
             'effects': [{'type': 'status', 'status': {'id': 'bw_frozen', 'flags': ['stun'], 'tags': ['debuff', 'magic']}, 'duration': round((lo + hi) / 2, 3),
                          'radius': r, 'kind': 'unit', 'inTransit': True, **owner_keys(ff.f.get('ownerFilter') if ff else 4, True)}]}]


@handler('UnitsInvisibility', 'UnitsDeposit')
def _(m, c):
    # Unit.invisible = invisibleTimer > fadeoutDuration: towers skip them (TowerModule) and skill filters miss them
    # (unitsMap); the timer counts down from invisibilityDuration (>= 10000: until the unit arrives)
    d = c.f(m, 'invisibilityDuration', 0) if m.cls == 'UnitsInvisibility' else 0
    if d > 10000:
        d = 0
    elif d:
        d = max(0.0, d - c.f(m, 'fadeoutDuration', 0.5))
    if m.cls == 'UnitsInvisibility':
        c.verified()
    return [c.status('bw_invisible', {'flags': ['invisible'], 'tags': ['buff']}, duration=d or c.duration, **ufilter(c))]


@handler('UnitsKill')
def _(m, c):
    # BW: every killEvery seconds (every tick when <= 0) while the element lives, kill all filtered units until
    # maxKills in total
    n = int(c.f(m, 'maxKills', -1))
    every = c.f(m, 'killEvery', -1)
    f = ufilter(c)
    if not c.duration:
        if n > 0:
            f['maxTargets'] = n
        return [dict({'type': 'kill'}, **f)]
    area = {'type': 'area', 'trigger': 'periodic', 'interval': round(every, 3) if every > 0 else 0.05, 'lifetime': round(c.duration, 3),
            'radius': f.get('radius', 1), 'fromCaster': False, 'effects': [dict({'type': 'kill'}, **f)]}
    del area['fromCaster']
    if n > 0:
        area['effects'][0]['maxTargets'] = n
        c.note('approx', 'maxKills caps each strike, not the total over the duration')
    if 'growing' in c.el_notes:
        c.note('approx', 'the BW kill radius grows with rangeVelocity x time; fixed here')
    return [area]


@handler('UnitsKillAura')
def _(m, c):
    # UnitsKillAura.UnitState: for killAuraDuration (infinite: until a tower hits it) the unit kills the first unit of
    # auraFilter (around itself, relative to its owner) every killAuraCollisionCheckCooldown s, is ignored by skill unit
    # filters, has attackCoefficient, and a tower shot does not kill it but cuts the time left to killAuraAfterDamageCooldown
    uf = c.ref(m, 'auraFilter')
    ak = ref_filter(c, uf) if uf else {'kind': 'unit', 'radius': 0.5, 'affects': 'enemies'}
    ak.pop('inTransit', None)
    ak['maxTargets'] = 1
    d = c.f(m, 'killAuraDuration', 5)
    if num(m.f.get('auraHasInfiniteDuration')):
        d = 0
    tick = {'interval': round(max(0.05, c.f(m, 'killAuraCollisionCheckCooldown', 0.2)), 3), 'effects': [dict({'type': 'kill'}, **ak)]}
    body = {'tags': ['buff'], 'flags': ['skillImmune'], 'tick': tick, 'hitCap': round(c.f(m, 'killAuraAfterDamageCooldown', 0.5), 3)}
    mod = stat_mod('damage', m.f.get('attackCoefficient'))
    if mod.get('mul', 1) != 1 or mod.get('add', 0):
        body['mods'] = [mod]
    c.verified()
    return [c.status('bw_kill_aura', body, duration=d, **ufilter(c))]


@handler('UnitsKillTrain', 'UnitsTornado', 'UnitsSandStorm', 'UnitsBlast', 'UnitsGravity', 'UnitsCharge')
def _(m, c):
    k = m.cls
    if k == 'UnitsTornado':
        per = c.f(m, 'killUnitsPerSecond', 5)
        mx = int(c.f(m, 'maximumUnitsToKill', 0))
        r = c.filter.get('radius', 2)
        hf = c.ref(m, 'houseFilter')
        eff = [dict({'type': 'kill', 'maxTargets': 1, 'radius': r}, **ufilter(c))]
        if hf:
            eff.append(dict({'type': 'garrisonKill', 'amount': 1, 'radius': c.cells(num(hf.f.get('inRange'))), 'kind': 'structure'},
                            **owner_keys(hf.f.get('ownerFilter'))))
        c.note('approx', 'tornado circles the point (BW: an expanding ellipse)')
        return [{'type': 'area', 'motion': 'wander', 'speed': 1, 'trigger': 'periodic', 'interval': round(1 / max(per, 0.1), 3),
                 'delay': round(c.f(m, 'beforeApplyDelay', 0), 3), 'lifetime': round(c.duration or 10, 3), 'maxTriggers': mx, 'radius': r, 'effects': eff}]
    if k == 'UnitsKillTrain':
        c.note('approx', 'train runs from the click along its direction killing on contact (environment object: agent-bw-env)')
        return [{'type': 'area', 'motion': 'toward', 'speed': c.speed(c.f(m, 'maxSpeed', 300)), 'trigger': 'contact', 'radius': 1, 'lifetime': round(c.duration or 10, 3),
                 'effects': [{'type': 'kill', 'shape': 'target', 'affects': 'all'}]}]
    if k == 'UnitsGravity':
        p = c.f(m, 'killProbability', 0.5)
        d = (c.f(m, 'minFlyDuration', 2) + c.f(m, 'maxFlyDuration', 4)) / 2
        return [c.status('bw_gravity', {'flags': ['stun'], 'tags': ['debuff'], 'mods': [{'stat': 'scale', 'mul': 1.3}],
                                        'onExpire': [{'type': 'kill', 'shape': 'target', 'chance': round(p, 4), 'affects': 'any'}]}, duration=d, **ufilter(c))]
    if k == 'UnitsBlast':
        return [dict({'type': 'knockback', 'amount': 3}, **ufilter(c))]
    if k == 'UnitsSandStorm':
        return [dict({'type': 'sendBack', 'mode': 'throwBack', 'amount': 3}, **ufilter(c))]
    c.note('approx', 'charging units: speed burst')
    return [c.status('bw_charge', {'tags': ['buff'], 'mods': [{'stat': 'speed', 'mul': 1.5}]}, **ufilter(c))]


@handler('UnitsMines')
def _(m, c):
    t, fl = c.f(m, 'selfDestructionTimeout', 10), c.f(m, 'selfDestructionFluctuation', 1)
    af = c.ref(m, 'affectFilter')
    r = c.cells(num(af.f.get('inRange'))) if af else 0.5
    area = {'type': 'area', 'trigger': 'contact', 'maxTriggers': 1, 'radius': r, 'lifetime': [round(max(0, t - fl), 3), round(t + fl, 3)],
            'origins': dict(ufilter(c), consume=True), 'effects': [dict({'type': 'kill', 'shape': 'target'}, **owner_keys(af.f.get('ownerFilter') if af else 4, True))]}
    if num(m.f.get('canDestroyUnitsInRange')):
        rf = c.ref(m, 'rangeDestroyFilter')
        area['onExpire'] = [dict({'type': 'kill', 'radius': c.cells(num(rf.f.get('inRange'))) if rf else r, 'kind': 'unit',
                                  'maxTargets': int(c.f(m, 'addiditionalUnitsCanBeDestroyedInRange', 2))}, **owner_keys(rf.f.get('ownerFilter') if rf else 4, True))]
    return [area]


@handler('UnitsJumpingHeads')
def _(m, c):
    dmg = c.f(m, 'damageToPoints', 1)
    return [{'type': 'area', 'goal': 'groupTarget', 'speed': c.speed(c.f(m, 'walkerVelocity', 150)), 'radius': 0.3,
             'origins': dict(ufilter(c), consume=True),
             'onArrive': [{'type': 'garrisonKill', 'amount': round(dmg, 3), 'shape': 'target', **owner_keys(m.f.get('attackBuildingFilter'))}]}]


@handler('UnitsPetrify')
def _(m, c):
    dmg = c.f(m, 'damageToPoints', 1)
    return [{'type': 'area', 'motion': 'lane', 'speed': c.speed(c.f(m, 'velocity', 150)), 'radius': 0.3,
             'origins': dict(ufilter(c), consume=True),
             'onArrive': [{'type': 'garrisonKill', 'amount': round(dmg, 3), 'shape': 'target', 'affects': 'any'}]}]


@handler('UnitsRage')
def _(m, c):
    mods = [stat_mod('damage', m.f.get('attackCoefficient')), stat_mod('speed', m.f.get('speedCoefficient'))]
    if num(m.f.get('useScale')):
        sc = m.f.get('scale') or {}
        mods.append({'stat': 'scale', 'add': round(num(sc.get('value')), 4)})
    if m.f.get('rageCoefficientMods'):
        c.note('approx', 'per-owner rage coefficients not converted')
    return [c.status('bw_rage', {'tags': ['buff', 'magic'], 'mods': mods}, **ufilter(c))]


@handler('UnitsReverse', 'UnitsWormReverse', 'UnitsGoHome', 'UnitsThrowBack', 'UnitsClone', 'UnitsAddSmithyLevel')
def _(m, c):
    k = m.cls
    if k == 'UnitsGoHome':
        return [dict({'type': 'sendBack', 'mode': 'home'}, **ufilter(c))]
    if k == 'UnitsThrowBack':
        return [dict({'type': 'sendBack', 'mode': 'throwBack', 'amount': 4}, **ufilter(c))]
    if k == 'UnitsClone':
        return [dict({'type': 'clone'}, **ufilter(c))]
    if k == 'UnitsAddSmithyLevel':
        return [c.status('bw_smithy', {'tags': ['buff'], 'mods': [{'stat': 'damage', 'add': round(c.f(m, 'smithyLevelIncrease', 1), 3)}]}, **ufilter(c))]
    out = [dict({'type': 'sendBack', 'mode': 'reverse'}, **ufilter(c))]
    if k == 'UnitsWormReverse':
        lo, hi = c.f(m, 'unitsSpeedMin', 1), c.f(m, 'unitsSpeedMax', 1.2)
        out.append(c.status('bw_worm_reverse', {'mods': [{'stat': 'speed', 'mul': round((lo + hi) / 2, 4)}]}, **ufilter(c)))
    return out


@handler('UnitsSlowdown', 'UnitsSlowdown2', 'UnitsSpeedCoefficient')
def _(m, c):
    if m.cls == 'UnitsSpeedCoefficient':
        st = {'tags': ['buff'], 'mods': [stat_mod('speed', m.f.get('value'))]}
        s1 = c.f(m, 'stage1Duration', -1)
        s2 = m.f.get('stage2SpeedMod')
        if s1 > 0 and isinstance(s2, dict):
            rest = max(0.0, (c.duration or 0) - s1)
            st['onExpire'] = [{'type': 'status', 'status': {'id': 'bw_speed_stage2', 'mods': [stat_mod('speed', s2)]}, 'duration': round(rest, 3),
                               'shape': 'target', 'affects': 'any'}]
            return [c.status('bw_speed', st, duration=s1, **ufilter(c))]
        return [c.status('bw_speed', st, **ufilter(c))]
    k = c.f(m, 'slowdownCoefficient', 0.5)
    if m.cls == 'UnitsSlowdown2':
        # UnitsSlowdown2.SlowdownUnits: speedCoefficient /= slowdownCoefficient for good, never below 1/3 (each cast
        # divides again: independent statuses, the product floored)
        c.verified()
        return [c.status('bw_slowdown2', {'tags': ['debuff', 'magic'], 'stacking': 'independent',
                                          'mods': [{'stat': 'speed', 'mul': round(1 / k if k else 1, 4), 'floor': round(1 / 3, 4)}]},
                         duration=0, **ufilter(c))]
    return [c.status('bw_slowdown', {'tags': ['debuff', 'magic'], 'mods': [{'stat': 'speed', 'mul': round(k, 4)}]}, **ufilter(c))]



# ============================================================================================ stage 2: checked against BW
# The handlers below replace the earlier ones; each was compared with the BW modificator code (named in the
# comment) and uses the engine primitives that reproduce it (docs/bunkerwars-skills.md).

def _dur(c):
    return round(c.duration, 3) if c.duration else 0


@handler('UnitsKill', 'PointsUnitsKill')
def _(m, c):
    # UnitsKill.Modificator.AdvanceTick: every killEvery s (every tick when <= 0) while the element lives, all units of the
    # filter die until maxKills in total; a units filter with rangeVelocity grows (inRange + velocity x time)
    n = int(c.f(m, 'maxKills', -1))
    every = c.f(m, 'killEvery', -1)
    f = ufilter(c)
    kill = dict({'type': 'kill'}, **f)
    if n >= 0:
        kill['budget'] = max(n, 0) if n > 0 else 0
        if n == 0:
            return []
    if c.grow > 0:
        kill['radius'] = 'area'
    if not c.duration:
        return [kill]
    area = {'type': 'area', 'trigger': 'periodic', 'interval': round(every, 3) if every > 0 else 0.05, 'lifetime': _dur(c),
            'radius': f.get('radius', 1), 'effects': [kill]}
    if c.grow > 0:
        area['grow'] = round(c.grow, 4)
    c.verified()
    return [area]


@handler('PlayersSilence')
def _(m, c):
    # PlayersSilence.Use: other players' skills (slotIndexes or all) are cancelled, get +duration cooldown and are blocked
    slots = [int(num(x)) for x in (m.f.get('slotIndexes') or [])]
    st = {'flags': ['silence'], 'tags': ['debuff', 'magic']}
    if slots:
        st['slots'] = slots
    out = [dict(c.status('bw_silence', st), type='playerStatus')]
    if c.duration:
        cd = dict({'type': 'cooldown', 'amount': _dur(c)}, **c.flt())
        if slots:
            cd['slots'] = slots
        out.append(cd)
    c.verified()
    return out


@handler('PointsPlague')
def _(m, c):
    # PointsPlague: decreaseUnitsPerSecond while active; units leaving carry it and infect with infectionChancePercent
    st = {'tags': ['debuff', 'magic'], 'flags': ['spreads'], 'garrisonPerSecond': -round(c.f(m, 'decreaseUnitsPerSecond', 2), 4),
          'tick': {'interval': 0.05}, 'spreadChance': round(c.f(m, 'infectionChancePercent', 100) / 100, 4)}
    if c.f(m, 'waveTimeout', 1) > 0:
        c.note('approx', 'BW lets a point be infected again only waveTimeout s after the last carrier entered')
    return [c.status('bw_plague', st, **sfilter(c))]


@handler('PointsManyBombs')
def _(m, c):
    # PointsManyBombs.Use: one bomb per point, dropped bulletCooldown apart; bulletCooldown = (timeout - dropTime - waveDuration)
    # / (count + 2); each falls dropTime s, kills killPercent % of the point and a wave kills units in waveFilter
    # (range + velocity x t) for waveDuration s
    pct = c.f(m, 'killPercent', 75) / 100
    drop, wave = c.f(m, 'dropTime', 2.5), c.f(m, 'waveDuration', 1)
    count = c.filter.get('maxTargets') or 1
    cool = max(0.0, ((c.duration or 0) - (drop + wave)) / (count + 2)) if c.duration else 0
    wf = c.ref(m, 'waveFilter')
    r0 = c.cells(num(wf.f.get('inRange'))) if wf else 1
    vel = c.speed(num(wf.f.get('rangeVelocity'), -1)) if wf and num(wf.f.get('rangeVelocity'), -1) > 0 else 0
    waveA = {'type': 'area', 'trigger': 'periodic', 'interval': 0.05, 'lifetime': round(wave, 3), 'radius': r0,
             'effects': [dict({'type': 'kill', 'kind': 'unit', 'radius': 'area' if vel else r0, 'aspect': c.aspect},
                              **owner_keys(wf.f.get('ownerFilter') if wf else 4, True))]}
    if vel:
        waveA['grow'] = vel
    bomb = {'type': 'area', 'delay': round(drop, 3), 'goal': 'target', 'speed': 1000, 'radius': 0.5,
            'onArrive': [{'type': 'garrisonKill', 'percent': round(pct, 4), 'shape': 'target', 'affects': 'any'}, waveA]}
    c.verified()
    return [dict({'type': 'cast', 'stagger': round(cool, 4), 'effects': [bomb]}, **sfilter(c))]


@handler('PlayersIncreaseMorale', 'PlayersTemporaryIncreaseMorale')
def _(m, c):
    # Player.AddMoraleLevel(increaseLevel + additionalIncreaseLevel): stars (level + progress) up by n
    lv = c.f(m, 'increaseLevel', 1) + c.f(m, 'additionalIncreaseLevel', 0)
    out = [dict({'type': 'resource', 'resource': 'morale', 'levels': round(lv, 4)}, **c.flt())]
    if c.f(m, 'moraleCooldown', 0):
        c.note('approx', 'moraleCooldown (decay timer shift) not converted')
    if c.f(m, 'lowMoraleEdge', 0) or c.f(m, 'giveMoraleOnStart', 0):
        c.note('approx', 'battle-start morale edge / start stars (OnBattleStarted) belong to the loadout setup')
    c.verified()
    return out


@handler('PointsMoraleThief')
def _(m, c):
    # PointsMoraleThief: starStealAmount stars move from the owner of the clicked point to the caster (after the star flies)
    # after pointAnimationDuration the point owner loses min(n, its stars); the caster gets them (Player.SetMoraleLevel on
    # its own level + progress) when the star lands, starFlyDuration s later
    n = c.f(m, 'starStealAmount', 1)
    wait = c.f(m, 'pointAnimationDuration', 0)
    c.note('approx', 'the caster gets the stars together with the loss (BW: starFlyDuration s later, after the flight)')
    take = dict({'type': 'resource', 'resource': 'morale', 'levels': -round(n, 4), 'transfer': True}, **sfilter(c, shape='target'))
    if wait > 0:
        return [{'type': 'area', 'delay': round(wait, 3), 'goal': 'target', 'speed': 1000, 'radius': 0.5,
                 'onArrive': [dict(take, shape='target', kind='structure')]}]
    return [take]


@handler('PlayersAllMorale')
def _(m, c):
    # PlayersAllMorale: morale to the top level while active, the saved points come back at the end
    c.note('approx', 'the previous morale is not restored when it ends')
    return [dict({'type': 'resource', 'resource': 'morale', 'levels': 'max'}, **c.flt())]


@handler('PointsAmulet')
def _(m, c):
    # PointsAmulet.ReverseUnits (every tick): groups of reverseUnitsFilter heading for the point turn round when within
    # reverseDistance of it and walk unitsSpeed faster, at most maxReverseCount units
    r = c.cells(c.f(m, 'reverseDistance', 100))
    mx = int(c.f(m, 'maxReverseCount', 50))
    tick = [dict({'type': 'sendBack', 'mode': 'reverse', 'radius': r, 'inTransit': True, 'heading': 'to', 'kind': 'unit', 'aspect': c.aspect},
                 **owner_keys(m.f.get('reverseUnitsFilter'), True))]
    if mx > 0:
        tick[0]['budget'] = mx
    sp = c.f(m, 'unitsSpeed', 1)
    if sp != 1:
        tick.append(dict({'type': 'status', 'status': {'id': 'bw_amulet_speed', 'mods': [{'stat': 'speed', 'mul': round(sp, 4)}]}, 'radius': r,
                          'inTransit': True, 'heading': 'from', 'kind': 'unit'}, **owner_keys(m.f.get('reverseUnitsFilter'), True)))
    c.note('approx', 'reverseDistance is measured straight (BW: along the route)')
    return [c.status('bw_amulet', {'tags': ['buff'], 'tick': {'interval': 0.05, 'effects': tick}}, **sfilter(c))]


@handler('PointsKillLeavingUnits')
def _(m, c):
    # PointsKillLeavingUnits.OnUnitCreated: every unit leaving the point dies delayToKill s later, at most maximumUnitsToKill
    lo, hi = c.f(m, 'delayToKillFrom', 0.9), c.f(m, 'delayToKillTo', 1.1)
    mx = int(c.f(m, 'maximumUnitsToKill', 100))
    st = {'id': 'bw_doomed', 'tags': ['debuff'], 'onExpire': [{'type': 'kill', 'shape': 'target', 'affects': 'any'}]}
    eff = {'type': 'status', 'status': st, 'duration': round((lo + hi) / 2, 3), 'shape': 'target', 'affects': 'any'}
    if mx > 0:
        eff['budget'] = mx
    if hi > lo:
        c.note('approx', f'kill delay {lo}..{hi} s as the mean')
    return [c.status('bw_kill_leaving', {'tags': ['debuff'], 'onSpawn': [eff]}, **sfilter(c))]


@handler('PointsChickenCoop')
def _(m, c):
    # PointsChickenCoop.OnUnitCreated: produced units x unitScaleCoefficient / unitSpeedCoefficient / unitAttackCoefficient
    mods = [{'stat': 'scale', 'mul': round(c.f(m, 'unitScaleCoefficient', 1), 4)}, {'stat': 'speed', 'mul': round(c.f(m, 'unitSpeedCoefficient', 1), 4)},
            {'stat': 'damage', 'mul': round(c.f(m, 'unitAttackCoefficient', 1), 4)}]
    spawn = [{'type': 'status', 'status': {'id': 'bw_chicken', 'mods': mods}, 'shape': 'target', 'affects': 'any'}]
    c.verified()
    return [c.status('bw_chicken_coop', {'tags': ['buff'], 'onSpawn': spawn}, **sfilter(c))]


@handler('PointsEnhanceWeapon')
def _(m, c):
    # PointsEnhanceWeapon.OnUnitCreated: units leaving get additionalWeapon smithy levels (fixedAdditionalSmithyLevel)
    lv = int(c.f(m, 'additionalWeapon', 0))
    if lv <= 0:
        return []
    mul = SMITHY_ATTACK[min(lv, len(SMITHY_ATTACK)) - 1]
    spawn = [{'type': 'status', 'status': {'id': 'bw_weapon', 'mods': [{'stat': 'damage', 'mul': mul}]}, 'shape': 'target', 'affects': 'any'}]
    c.note('approx', 'extra smithy levels as the armory attack factor of that many levels without armories (BW adds them to the owned ones)')
    return [c.status('bw_enhance_weapon', {'tags': ['buff'], 'onSpawn': spawn}, **sfilter(c))]


@handler('PointsHauntedHouse')
def _(m, c):
    # PointsHauntedHouse: unitsCount units appear in the point and vanish when the skill ends (unless it was captured)
    n = round(c.f(m, 'unitsCount', 10), 3)
    st = {'tags': ['buff'], 'endOnCapture': True,
          'onExpire': [{'type': 'garrisonKill', 'amount': n, 'shape': 'target', 'affects': 'any'}]}
    c.verified()
    return [dict({'type': 'garrisonAdd', 'amount': n}, **sfilter(c, affects=c.filter.get('affects', 'any'))),
            c.status('bw_haunted', st, **sfilter(c, affects=c.filter.get('affects', 'any')))]


@handler('PointsEndGameOnCatch')
def _(m, c):
    # PointsEndGameOnCatch.OnOwnerChanged: the match ends, the enemies of the local player are defeated
    c.note('approx', 'BW defeats the enemies of the local player; here the enemies of the skill owner')
    return [c.status('bw_end_game', {'tags': ['buff'], 'onCaptured': [{'type': 'defeat', 'shape': 'map', 'affects': 'enemies'}]},
                     **sfilter(c, affects=c.filter.get('affects', 'any')))]


@handler('PointsHypnosis')
def _(m, c):
    # PointsHypnosis: the caster may send units out of the enemy point once (percent %); captureHypnotisedUnits makes them his
    pct = c.f(m, 'percent', 0)
    cmd = {'percent': round(pct / 100, 4)} if pct > 0 else {}
    if num(m.f.get('captureHypnotisedUnits')):
        cmd['capture'] = True
    if num(m.f.get('unitsAreIllegible')):
        c.note('approx', 'illegible units (they also hurt their own points) are not converted')
    c.verified()
    return [c.status('bw_hypnosis', {'tags': ['debuff', 'magic'], 'command': cmd}, **sfilter(c))]


@handler('PointsTeleport')
def _(m, c):
    # PointsTeleport.Use: the next send from the point grabs unitsTeleportPercent % over grabUnitsDuration, the dig wave
    # travels the route at waveVelocity, the target fills over fillUnitsDuration
    t = {'grab': round(c.f(m, 'grabUnitsDuration', 0), 3), 'fill': round(c.f(m, 'fillUnitsDuration', 0), 3),
         'speed': c.speed(c.f(m, 'waveVelocity', 200)), 'percent': round(c.f(m, 'unitsTeleportPercent', 50) / 100, 4)}
    c.verified()
    return [c.status('bw_teleport', {'tags': ['buff'], 'transfer': t}, **sfilter(c))]


@handler('UnitsTornado')
def _(m, c):
    # UnitsTornado: after beforeApplyDelay the tornado circles the click on an ellipse (ellipseSize / 2, growing over
    # expandEllipseDuration, angularSpeed deg / s); every tick it kills the units of the element filter around it and every
    # 1 / killUnitsPerSecond s one unit of each point of houseFilter (range = the element range), maximumUnitsToKill in total
    es = m.f.get('ellipseSize') or {}
    orbit = {'a': c.cells(num(es.get('x')) / 2), 'b': c.cells(num(es.get('y')) / 2), 'speed': round(c.f(m, 'angularSpeed', 180), 3),
             'expand': round(c.f(m, 'expandEllipseDuration', 0), 3)}
    mx = int(c.f(m, 'maximumUnitsToKill', 0))
    per = max(c.f(m, 'killUnitsPerSecond', 5), 0.01)
    r = c.filter.get('radius', 1)
    kill = dict({'type': 'kill'}, **ufilter(c))
    if mx > 0:
        kill['budget'] = mx
    life = _dur(c) or 10
    delay = round(c.f(m, 'beforeApplyDelay', 0), 3)
    out = [{'type': 'area', 'motion': 'orbit', 'orbit': orbit, 'trigger': 'periodic', 'interval': 0.05, 'delay': delay, 'lifetime': life,
            'radius': r, 'effects': [kill]}]
    hf = c.ref(m, 'houseFilter')
    if hf:
        gk = dict({'type': 'garrisonKill', 'amount': 1, 'radius': r, 'kind': 'structure', 'aspect': c.aspect}, **owner_keys(hf.f.get('ownerFilter')))
        tags = point_tags(hf.f.get('typeFilter'))
        if tags:
            gk['tags'] = tags
        if mx > 0:
            gk['budget'] = mx
        out.append({'type': 'area', 'motion': 'orbit', 'orbit': orbit, 'trigger': 'periodic', 'interval': round(1 / per, 4), 'delay': delay,
                    'lifetime': life, 'radius': r, 'effects': [gk]})
        if mx > 0:
            c.note('approx', 'maximumUnitsToKill caps units and building kills separately (BW: one counter)')
    return out


@handler('PointsFlyingUnits')
def _(m, c):
    # PointsFlyingUnits: groups sent from the point fly (BW flying routes, fly row spacing) at speedCoefficient, attack
    # attackCoefficient, up to maxValue units
    sp, at = round(c.f(m, 'speedCoefficient', 1.2), 4), round(c.f(m, 'attackCoefficient', 1), 4)
    spawn = [{'type': 'status', 'status': {'id': 'bw_flyer', 'flags': ['flying'], 'mods': [{'stat': 'speed', 'mul': sp}, {'stat': 'damage', 'mul': at}]},
              'shape': 'target', 'affects': 'any'}]
    mx = int(c.f(m, 'maxValue', -1))
    if mx > 0:
        spawn[0]['budget'] = mx
    if num(m.f.get('ignoredByTowers')) or num(m.f.get('ignoredBySkills')):
        c.note('approx', 'ignoredByTowers / ignoredBySkills not converted')
    return [c.status('bw_flying_point', {'flags': ['flying'], 'tags': ['buff'], 'onSpawn': spawn}, **sfilter(c))]


@handler('PositionFlyingTower')
def _(m, c):
    # PositionFlyingTower: a temporary tower at the click (agent-bw-routes: type "ufo" with rangeAspect weapons)
    c.note('ref', 'flying tower type "ufo" (agent-bw-routes); its defense aura additionalDefenseCoefficient as a defense status')
    out = [{'type': 'spawn', 'unit': 'ufo', 'duration': _dur(c)}]
    add = c.f(m, 'additionalDefenseCoefficient', 0)
    if add:
        out.append({'type': 'area', 'motion': 'static', 'trigger': 'periodic', 'interval': 0.25, 'lifetime': _dur(c), 'radius': c.cells(c.f(m, 'range', 200)),
                    'effects': [{'type': 'status', 'status': {'id': 'bw_ufo_defense', 'mods': [{'stat': 'defense', 'add': round(add, 4)}]},
                                 'duration': 0.3, 'radius': c.cells(c.f(m, 'range', 200)), 'kind': 'structure', 'affects': 'own'}]})
    return out


@handler('PointsSkeletonArmy', 'PointsGhostArmy')
def _(m, c):
    n = int(c.f(m, 'armyCount' if m.cls == 'PointsSkeletonArmy' else 'unitsCount', 10))
    c.note('approx', 'army as soldiers of the caster next to the point (BW skeleton skins, appear animation)')
    return [{'type': 'spawn', 'unit': 'soldier', 'count': n}]


@handler('PointsBarrier')
def _(m, c):
    c.note('approx', 'wall across the lanes at the click: a "bw_barrier" unit with blocksLanes and hp = unitsCount')
    return [{'type': 'spawn', 'unit': 'bw_barrier', 'duration': _dur(c)}]


@handler('PointsCastBuilding')
def _(m, c):
    to = point_tags(m.f.get('castType')) if num(m.f.get('castToType'), 1) else []
    lvl = int(c.f(m, 'targetCastLevel', -1))
    keep = round(c.f(m, 'chanceToSaveLevelAfterDecast', 0), 4)
    if not to:
        e = {'type': 'level', 'delta': 1}
        c.note('approx', 'cast to the same kind at targetCastLevel as a +1 level for the duration')
    else:
        e = {'type': 'transform', 'to': KIND_TYPE.get(to[0], to[0]) + str(lvl if lvl > 0 else 1), 'keepChance': keep}
        c.note('approx', 'cast keeps the target level only (BW castToMaxInsteadMin / forceCastLevel / maxCastLevel)')
    if c.duration:
        e['duration'] = _dur(c)
    return [dict(e, **sfilter(c, affects=c.filter.get('affects', 'any')))]


@handler('UnitsMines')
def _(m, c):
    # UnitsMines.Use: every filtered unit becomes a mine (the unit is gone); a mine kills the first unit of affectFilter that
    # touches it (checked every mineCollisionCheckCooldown), self-destructs after selfDestructionTimeout +- fluctuation and
    # then kills addiditionalUnitsCanBeDestroyedInRange units of rangeDestroyFilter (canDestroyUnitsInRange)
    t, fl = c.f(m, 'selfDestructionTimeout', 10), c.f(m, 'selfDestructionFluctuation', 1)
    af = c.ref(m, 'affectFilter')
    r = c.cells(num(af.f.get('inRange'))) if af else 0.5
    area = {'type': 'area', 'trigger': 'contact', 'maxTriggers': 1, 'radius': r, 'lifetime': [round(max(0, t - fl), 3), round(t + fl, 3)],
            'origins': dict(ufilter(c), consume=True), 'effects': [dict({'type': 'kill', 'shape': 'target'}, **owner_keys(af.f.get('ownerFilter') if af else 4, True))]}
    if num(m.f.get('canDestroyUnitsInRange')):
        rf = c.ref(m, 'rangeDestroyFilter')
        area['onExpire'] = [dict({'type': 'kill', 'radius': c.cells(num(rf.f.get('inRange'))) if rf else r, 'kind': 'unit', 'aspect': c.aspect,
                                  'maxTargets': int(c.f(m, 'addiditionalUnitsCanBeDestroyedInRange', 2))}, **owner_keys(rf.f.get('ownerFilter') if rf else 4, True))]
    c.verified()
    return [area]



@handler('UnitsReverse')
def _(m, c):
    # UnitsReverse.AdvanceTick: every unit of the filter turns round once per skill while the element lives
    sb = dict({'type': 'sendBack', 'mode': 'reverse', 'shape': 'target'})
    if not c.duration:
        return [dict({'type': 'sendBack', 'mode': 'reverse'}, **ufilter(c))]
    f = ufilter(c)
    area = dict({'type': 'area', 'trigger': 'contact', 'lifetime': _dur(c), 'radius': f.get('radius', 1), 'effects': [sb]},
                **{k: v for k, v in f.items() if k not in ('radius', 'shape')})
    c.verified()
    return [area]


@handler('UnitsFreeze', 'PointsFreeze', 'PointsStoneUnits')
def _(m, c):
    # UnitsFreeze.AdvanceTick: every tick while the element lives, units of the filter that are not frozen freeze for
    # random(minFreezeDuration, maxFreezeDuration) s
    lo, hi = c.f(m, 'minFreezeDuration', 0), c.f(m, 'maxFreezeDuration', 0)
    if m.cls == 'PointsStoneUnits':
        lo = hi = c.f(m, 'freezeDuration', 4)
    dur = [round(lo, 3), round(hi, 3)] if hi > lo else round(lo or c.duration, 3)
    st = {'id': 'bw_frozen', 'flags': ['stun'], 'tags': ['debuff', 'magic'], 'stacking': 'keep'}
    if m.cls == 'PointsFreeze':
        return [c.status('bw_frozen', dict(st, id='bw_frozen'), duration=(lo + hi) / 2 or c.duration, **sfilter(c))]
    f = ufilter(c)
    eff = dict({'type': 'status', 'status': st, 'duration': dur}, **f)
    if not c.duration:
        return [eff]
    c.verified()
    return [{'type': 'area', 'trigger': 'periodic', 'interval': 0.05, 'lifetime': _dur(c), 'radius': f.get('radius', 1), 'effects': [eff]}]


@handler('UnitsSpeedCoefficient')
def _(m, c):
    # UnitsSpeedCoefficient.Use: units of the filter get speed x value for good; with stage1Duration the value is undone
    # after it and stage2SpeedMod applies instead
    st = {'tags': ['buff'], 'mods': [stat_mod('speed', m.f.get('value'))]}
    s1 = c.f(m, 'stage1Duration', -1)
    s2 = m.f.get('stage2SpeedMod')
    if s1 > 0 and isinstance(s2, dict):
        st['onExpire'] = [{'type': 'status', 'status': {'id': 'bw_speed_stage2', 'mods': [stat_mod('speed', s2)]}, 'shape': 'target', 'affects': 'any'}]
        c.verified()
        return [c.status('bw_speed', st, duration=s1, **ufilter(c))]
    c.verified()
    return [c.status('bw_speed', st, duration=0, **ufilter(c))]


@handler('PointsPanic')
def _(m, c):
    # PointsPanic.MoveUnits: panicPercent % of each point walks out to the maxPoints nearest (route length) points of
    # runToFilter (relative to the point owner), ceil(units / count) each, as sends of the owner
    pct = c.f(m, 'panicPercent', 100) / 100
    e = dict({'type': 'garrisonSend', 'percent': round(pct, 4), 'maxPoints': max(1, int(c.f(m, 'maxPoints', 1))),
              'runTo': owner_keys(m.f.get('runToFilter'))}, **sfilter(c))
    if c.f(m, 'chanceToNeutral', 0) > 0:
        c.note('approx', 'chanceToNeutral (panicking groups turn neutral skeletons) not converted')
    ak = m.f.get('attackCoefficient')
    if isinstance(ak, dict) and num(ak.get('value'), 1) != 1:
        c.note('approx', 'group attackCoefficient not converted')
    c.verified()
    return [e]


@handler('PointsWarChains', 'PointsDeadLock')
def _(m, c):
    # PointsWarChains.Use: groups still leaving the points are closed (the rest of their units stay home)
    if m.cls == 'PointsDeadLock':
        c.note('approx', 'DeadLock as closing the groups of the point')
    else:
        c.verified()
    return [dict({'type': 'closeGroups'}, **sfilter(c))]


@handler('UnitsTornado')
def _(m, c):
    # see the comment of the earlier handler: one counter for units and building kills (BW unitsKilled)
    es = m.f.get('ellipseSize') or {}
    orbit = {'a': c.cells(num(es.get('x')) / 2), 'b': c.cells(num(es.get('y')) / 2), 'speed': round(c.f(m, 'angularSpeed', 180), 3),
             'expand': round(c.f(m, 'expandEllipseDuration', 0), 3)}
    mx = int(c.f(m, 'maximumUnitsToKill', 0))
    per = max(c.f(m, 'killUnitsPerSecond', 5), 0.01)
    r = c.filter.get('radius', 1)
    effs = [dict({'type': 'kill'}, **ufilter(c))]
    hf = c.ref(m, 'houseFilter')
    if hf:
        gk = dict({'type': 'garrisonKill', 'amount': 1, 'radius': r, 'kind': 'structure', 'aspect': c.aspect, 'every': round(1 / per, 4)},
                  **owner_keys(hf.f.get('ownerFilter')))
        tags = point_tags(hf.f.get('typeFilter'))
        if tags:
            gk['tags'] = tags
        effs.append(gk)
    if mx > 0:
        for e in effs:
            e['budget'] = mx
    c.verified()
    return [{'type': 'area', 'motion': 'orbit', 'orbit': orbit, 'trigger': 'periodic', 'interval': 0.05,
             'delay': round(c.f(m, 'beforeApplyDelay', 0), 3), 'lifetime': _dur(c) or 10, 'radius': r, 'effects': effs}]


@handler('PointsKillLeavingUnits')
def _(m, c):
    lo, hi = c.f(m, 'delayToKillFrom', 0.9), c.f(m, 'delayToKillTo', 1.1)
    mx = int(c.f(m, 'maximumUnitsToKill', 100))
    st = {'id': 'bw_doomed', 'tags': ['debuff'], 'onExpire': [{'type': 'kill', 'shape': 'target', 'affects': 'any'}]}
    eff = {'type': 'status', 'status': st, 'duration': [round(lo, 3), round(hi, 3)] if hi > lo else round(lo, 3), 'shape': 'target', 'affects': 'any'}
    if mx > 0:
        eff['budget'] = mx
    c.verified()
    return [c.status('bw_kill_leaving', {'tags': ['debuff'], 'onSpawn': [eff]}, **sfilter(c))]


def convert_modificator(m, ctx):
    fn = H.get(m.cls)
    if not fn:
        ctx.note('missing', f'{m.cls}: not mapped')
        return []
    out = fn(m, ctx)
    if ctx.fidelity == 'mapped' and (m.cls in VERIFIED or ctx._verified):
        ctx.fidelity = 'exact'
    ch = num(m.f.get('chance'))
    if ch and out:
        p = num(m.f.get('chanceValue'), 1)
        out = [{'type': 'cast', 'chance': round(p, 4), 'effects': out}]
    delay = num(m.f.get('additionalActivationDelay'), -1)
    if delay > 0 and out:
        out = [{'type': 'area', 'delay': round(delay, 3), 'radius': 0.5, 'effects': out}]
    return out
