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
    5: {'affects': 'enemies', 'neutral': True, 'neutralOnly': True},   # Neutral (approximated: enemies + neutral)
    6: {'affects': 'enemies', 'neutral': True},  # EnemiesAndNeutral
    7: {'affects': 'all'},                       # AllExceptMe (approximated)
    8: {'affects': 'enemies', 'neutral': True},  # AllExceptAllies
}
# PointInfo.Type bits -> structure tags of the imported buildings (agent-bw-data names them the same way)
POINT_TYPES = [(0x1, 'house'), (0x2, 'tower'), (0x4, 'smithy'), (0x8, 'special1'), (0x10, 'domination'), (0x20, 'kingOfTheHill'),
               (0x40, 'cave'), (0x80, 'graveyard'), (0x100, 'special2'), (0x200, 'special3'), (0x400, 'special4'), (0x800, 'special5')]


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

    def cells(self, v):
        return round(num(v) / self.px, 3)

    def speed(self, v):
        return round(num(v) / self.px, 3)

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
        out.append(dict({'type': 'swap'}, **sfilter(c)))
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
    for dm in mods if isinstance(mods, list) else []:
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
    ups = c.f(m, 'unitsPerSecond', 0)
    if ups > 0:
        c.note('approx', 'BW keeps it while units arrive slower than unitsPerSecond; here: for the duration')
    return [c.status('bw_invulnerable', {'flags': ['invulnerable'], 'tags': ['buff', 'magic']}, **sfilter(c))]


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
    e = {'type': 'redirect'}
    if c.duration:
        e['duration'] = round(c.duration, 3)
    return [dict(e, **sfilter(c))]


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
    if m.cls == 'UnitsExploring':
        return [dict(c.status('bw_exploring', {'flags': ['alert'], 'tags': ['buff']}), type='playerStatus', affects='self')]
    return [c.status('bw_spy', {'flags': ['revealed'], 'tags': ['debuff']}, **sfilter(c))]


@handler('PointsSuppressDefense')
def _(m, c):
    s = c.f(m, 'supressDefenseCoefficient', 0)
    c.note('approx', 'Lerp(defense, 1, s) as defense x (1 - s)')
    return [c.status('bw_suppress', {'tags': ['debuff'], 'mods': [{'stat': 'defense', 'mul': round(1 - s, 4)}]}, **sfilter(c))]


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
    c.note('approx', 'own units in range die; the morale gained per unit is not converted')
    return [dict({'type': 'kill'}, **c.flt(kind='unit'))]


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

@handler('PositionBlindTowers')
def _(m, c):
    bf = c.ref(m, 'blindBuildingFilter')
    out = [c.status('bw_blind', {'flags': ['blind'], 'tags': ['debuff']}, kind='structure', shape='map',
                    tags=point_tags(bf.f.get('typeFilter')) if bf else ['tower'], **owner_keys(bf.f.get('ownerFilter') if bf else 4))]
    k = c.f(m, 'unitsSpeedCoefficient', 0.7)
    if k != 1:
        out.append({'type': 'playerStatus', 'status': {'id': 'bw_blind_speed', 'mods': [{'stat': 'speed', 'mul': round(k, 4)}]},
                    'duration': round(c.duration or 0, 3), **owner_keys(m.f.get('applySpeedTo'))})
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
    n = int(c.f(m, 'unitsMaxCount', 0)) if m.cls == 'UnitsBetrayalGarden2' else 0
    f = ufilter(c)
    if n > 0:
        f['maxTargets'] = n
    out = [dict({'type': 'convert'}, **f)]
    if m.cls == 'UnitsBetrayalGarden2':
        out.append(c.status('bw_betrayal', {'tags': ['debuff'], 'mods': [stat_mod('speed', m.f.get('speedCoefficient')), stat_mod('damage', m.f.get('attackCoefficient'))]},
                            **dict(f, affects='own')))
    return out


@handler('UnitsBlizzardRain')
def _(m, c):
    n = max(1, int(c.f(m, 'bulletsCount', 10)))
    per = c.f(m, 'pointKillPerBullet', 1)
    life = c.duration or 5
    r = c.filter.get('radius', 3)
    uf, pf = c.ref(m, 'unitsFilter'), c.ref(m, 'pointsFilter')
    hit = []
    if uf:
        hit.append(dict({'type': 'kill', 'radius': c.cells(num(uf.f.get('inRange'))), 'kind': 'unit'}, **owner_keys(uf.f.get('ownerFilter'), True)))
    if pf:
        hit.append(dict({'type': 'garrisonKill', 'amount': round(per, 3), 'radius': c.cells(num(pf.f.get('inRange'))), 'kind': 'structure'},
                        **owner_keys(pf.f.get('ownerFilter'))))
    return [{'type': 'area', 'trigger': 'periodic', 'interval': round(life / n, 3), 'lifetime': round(life, 3), 'radius': r,
             'effects': [{'type': 'projectile', 'speed': 8, 'arc': True, 'scatter': r, 'effects': hit}]}]


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
    d = c.f(m, 'invisibilityDuration', 0) if m.cls == 'UnitsInvisibility' else 0
    if d > 10000:
        d = 0
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
    uf = c.ref(m, 'auraFilter')
    r = c.cells(num(uf.f.get('inRange'))) if uf else 0.5
    d = c.f(m, 'killAuraDuration', 5)
    if num(m.f.get('auraHasInfiniteDuration')):
        d = 0
    tick = {'interval': round(max(0.05, c.f(m, 'killAuraCollisionCheckCooldown', 0.2)), 3),
            'effects': [dict({'type': 'kill', 'radius': r, 'kind': 'unit', 'maxTargets': 1}, **owner_keys(uf.f.get('ownerFilter') if uf else 4, True))]}
    return [c.status('bw_kill_aura', {'tags': ['buff'], 'tick': tick, 'mods': [stat_mod('damage', m.f.get('attackCoefficient'))]}, duration=d, **ufilter(c))]


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
        c.note('approx', 'slowdown grows toward maxSlowdownCoefficient in BW; constant here')
    return [c.status('bw_slowdown', {'tags': ['debuff', 'magic'], 'mods': [{'stat': 'speed', 'mul': round(k, 4)}]}, **ufilter(c))]


def convert_modificator(m, ctx):
    fn = H.get(m.cls)
    if not fn:
        ctx.note('missing', f'{m.cls}: not mapped')
        return []
    out = fn(m, ctx)
    if ctx.fidelity == 'mapped' and m.cls in VERIFIED:
        ctx.fidelity = 'exact'
    ch = num(m.f.get('chance'))
    if ch and out:
        p = num(m.f.get('chanceValue'), 1)
        out = [{'type': 'cast', 'chance': round(p, 4), 'effects': out}]
    delay = num(m.f.get('additionalActivationDelay'), -1)
    if delay > 0 and out:
        out = [{'type': 'area', 'delay': round(delay, 3), 'radius': 0.5, 'effects': out}]
    return out
