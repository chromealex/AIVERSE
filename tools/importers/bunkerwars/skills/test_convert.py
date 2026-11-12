#!/usr/bin/env python3
"""Self-test of the Bunker Wars skills converter on a tiny synthetic client (no BW data needed):
Unity YAML reading, level upgrades through interface properties, filters -> targets, modificators -> effects.

    python3 tools/importers/bunkerwars/skills/test_convert.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert  # noqa: E402
import unityyaml  # noqa: E402

G = {  # script guids
    'SkillData': '59c223b35ff2c8f4591ed9e8d4e83165', 'MapClick': 'a1', 'Timeout': 'a2', 'UnitsFilter': 'a3', 'PointsFilter': 'a4',
    'UnitsRage': 'a5', 'SkillCooldown': 'a6', 'SkillEffectForce': 'a7', 'SkillRange': 'a8', 'SkillsUpgradeInfo': 'a9',
    'SkillDescriptionInfo': 'b1', 'Header': 'b2', 'PointsBomb': 'b3', 'SkillUnitsCount': 'b4', 'UnitsMines': 'b5', 'SkillEffectDuration': 'b6',
}
CS = {
    'Database/EnergySkills/SkillData.cs': 'public class SkillData : MonoBehaviour { }',
    'Database/EnergySkills/Conditions/SkillModificatorConditionMapClick.cs': 'public class SkillModificatorConditionMapClick : SkillModifyConditionInfoBase { }',
    'Database/EnergySkills/Conditions/SkillModificatorConditionTimeout.cs':
        'public class SkillModificatorConditionTimeout : SkillModifyConditionInfoBase, IModifiableEffectDurationSingle { public float time;\n'
        ' public float effectDuration { get => this.time; set => this.time = value; } }',
    'Database/EnergySkills/Filters/SkillRangeFilterBase.cs': 'public class SkillRangeFilterBase : SkillFilterBase { public float inRange = -1f;\n'
        ' public float startRange { get => this.inRange; set => this.inRange = value; } }',
    'Database/EnergySkills/Filters/SkillUnitsFilter.cs': 'public class SkillUnitsFilter : SkillRangeFilterBase { public int maxCount = -1; }',
    'Database/EnergySkills/Filters/SkillPointsFilter.cs': 'public class SkillPointsFilter : SkillRangeFilterBase { public bool oneNearest; }',
    'Database/EnergySkills/Modificators/SkillModificatorInfoBase.cs': 'public abstract class SkillModificatorInfoBase : MonoBehaviour { }\n'
        'public abstract class SkillUnitsModificatorInfoBase : SkillModificatorInfoBase { }\npublic abstract class SkillPointModificatorInfoBase : SkillModificatorInfoBase { }',
    'Database/EnergySkills/Modificators/Units/UnitsRage.cs':
        'public class UnitsRage : SkillUnitsModificatorInfoBase, IModifiableUnitsSpeed, IModifiableEffectForce {\n'
        ' public class Modificator : UnitsModificatorBase { }\n public FloatValue attackCoefficient; public FloatValue speedCoefficient;\n'
        ' public float unitsEffectSpeed { get => this.attackCoefficient.value; set => this.attackCoefficient.value = value; }\n'
        ' public float effectForce { get => this.speedCoefficient.value; set => this.speedCoefficient.value = value; } }',
    'Database/EnergySkills/Modificators/Points/PointsBomb.cs':
        'public class PointsBomb : SkillPointModificatorInfoBase, IModifiableUnitsCount { public SkillFilterBase waveFilter; public float killPercent = 75f;\n'
        ' public float waveDelay = 2f; public float effectUnitsCount { get => this.killPercent; set => this.killPercent = value; } }',
    'Database/EnergySkills/Modificators/Units/UnitsMines.cs':
        'public class UnitsMines : SkillUnitsModificatorInfoBase, IModifiableEffectDurationSingle { public SkillFilterBase affectFilter;\n'
        ' public float selfDestructionTimeout = 10f; public float selfDestructionFluctuation = 1f;\n'
        ' public float effectDuration { get => this.selfDestructionTimeout; set => this.selfDestructionTimeout = value; } }',
}
for n in ('SkillCooldown', 'SkillEffectForce', 'SkillRange', 'SkillUnitsCount', 'SkillEffectDuration'):
    CS[f'Database/EnergySkills/SkillUpgrades/{n}.cs'] = f'public class {n} : SkillDataModificatorBase {{ }}'
CS['Database/EnergySkills/SkillsUpgradeInfo.cs'] = 'public class SkillsUpgradeInfo : MonoBehaviour { }'
CS['Database/EnergySkills/SkillDescriptionInfo.cs'] = 'public class SkillDescriptionInfo : MonoBehaviour { }'
CS['Database/EnergySkills/Header.cs'] = 'public class Header : ScriptableObject { }'
SCRIPT_OF = {'SkillModificatorConditionMapClick.cs': 'MapClick', 'SkillModificatorConditionTimeout.cs': 'Timeout', 'SkillUnitsFilter.cs': 'UnitsFilter',
             'SkillPointsFilter.cs': 'PointsFilter'}


def mono(fid, script, body):
    lines = [f'--- !u!114 &{fid}', 'MonoBehaviour:', f'  m_Script: {{fileID: 11500000, guid: {G[script]}, type: 3}}']
    return '\n'.join(lines + ['  ' + l for l in body.strip('\n').split('\n')]) + '\n'


RAGE = ('%YAML 1.1\n--- !u!1 &1\nGameObject:\n  m_Name: _Skill7_Config_Rage\n' +
        mono(10, 'SkillData', '''clientId: 7
cooldown:
  key: hero2|skill3|cooldown
initialCooldown: 0
items:
- isSpendsEnergy: 1
  activateConditionsInfo:
  - {fileID: 11}
  deactivateConditionsInfo:
  - {fileID: 12}
  filter: {fileID: 13}
  modificators:
  - {fileID: 14}
''') + mono(11, 'MapClick', 'cancelOnRightClick: 1') + mono(12, 'Timeout', 'time: 8') +
        mono(13, 'UnitsFilter', 'inRange: 250\nownerFilter: 2\nmaxCount: -1') +
        mono(14, 'UnitsRage', '''chance: 0
useScale: 1
scale: {sign: 0, valueType: 0, value: 0.1, fromValue: 0, toValue: 0}
attackCoefficient: {sign: 2, valueType: 0, value: 1.5,
  fromValue: 0, toValue: 0}
speedCoefficient:
  sign: 2
  value: 1.05
'''))
BOMB = ('--- !u!1 &1\nGameObject:\n  m_Name: _Skill9_Bomb\n' +
        mono(10, 'SkillData', '''clientId: 9
items:
- isSpendsEnergy: 1
  activateConditionsInfo:
  - {fileID: 11}
  deactivateConditionsInfo: []
  filter: {fileID: 13}
  modificators:
  - {fileID: 14}
''') + mono(11, 'MapClick', 'cancelOnRightClick: 1') + mono(13, 'PointsFilter', 'inRange: 100\nownerFilter: 4\noneNearest: 1') +
        mono(14, 'PointsBomb', 'killPercent: 60\nwaveDelay: 1.5\nwaveFilter: {fileID: 15}') + mono(15, 'UnitsFilter', 'inRange: 150\nownerFilter: 4'))
MINES = ('--- !u!1 &1\nGameObject:\n  m_Name: _Skill16_Mines\n' +
         mono(10, 'SkillData', '''clientId: 16
items:
- isSpendsEnergy: 1
  activateConditionsInfo:
  - {fileID: 11}
  deactivateConditionsInfo: []
  filter: {fileID: 13}
  modificators:
  - {fileID: 14}
''') + mono(11, 'MapClick', 'x: 0') + mono(13, 'UnitsFilter', 'inRange: 200\nownerFilter: 4\nmaxCount: 15') +
         mono(14, 'UnitsMines', 'selfDestructionTimeout: 10\nselfDestructionFluctuation: 1\naffectFilter: {fileID: 15}\ncanDestroyUnitsInRange: 0') +
         mono(15, 'UnitsFilter', 'inRange: 30\nownerFilter: 4'))


def level(name, desc_guid, mods):
    body = '--- !u!1 &1\nGameObject:\n  m_Name: %s\n' % name
    ids = [f'  - {{fileID: {100 + i}}}' for i in range(len(mods))]
    body += mono(99, 'SkillsUpgradeInfo', f'skillDescription: {{fileID: 5, guid: {desc_guid}, type: 3}}\nmods:\n' + '\n'.join(ids))
    for i, (cls, fields) in enumerate(mods):
        body += mono(100 + i, cls, fields)
    return body


class ConverterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        A = os.path.join(self.tmp.name, 'Client', 'Assets')
        self.A = A

        def put(rel, text, guid=None):
            p = os.path.join(A, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'w') as f:
                f.write(text)
            if guid:
                with open(p + '.meta', 'w') as f:
                    f.write(f'fileFormatVersion: 2\nguid: {guid}\n')
        for rel, src in CS.items():
            base = os.path.basename(rel)
            key = SCRIPT_OF.get(base) or base[:-3].replace('SkillModificatorCondition', '')
            put('MW2Data/Gameplay/' + rel, 'namespace X {\n' + src + '\n}\n', G.get(key, 'z' + base))
        put('MW2Data/Resources/MW2.Modules/GameData/GameDataSettings.asset',
            '  data:\n    values:\n    - enabled: 1\n      values:\n      - 1\n      - 70\n    keys:\n    - version\n    - hero2|skill3|cooldown\n    keyHashes: x\n')
        sd = 'ME.TexturesGenerator/_Gameplay/SkillsData/'
        hd = 'ME.TexturesGenerator/_DatabaseData/GameInfo/SkillsHeaders/'
        put(sd + '_Skill7_Config_Rage.prefab', RAGE, 'c7')
        put(sd + '_Skill9_Bomb.prefab', BOMB, 'c9')
        put(sd + '_Skill16_Mines.prefab', MINES, 'c16')
        for n, base, cost in (('Skill7', 'c7', 350), ('Skill9', 'c9', 100), ('Skill16', 'c16', 300)):
            put(hd + f'SkillsData/{n}/{n}_Desc.prefab', mono(5, 'SkillDescriptionInfo', f'cost: {cost}\nskillResource:\n  guid: {base}\n  resourcePath: x'), 'd' + n)
        put(hd + 'SkillsData/Skill7/Skill7_Level1.prefab', level('L1', 'dSkill7', []), 'l71')
        put(hd + 'SkillsData/Skill7/Skill7_Level2.prefab', level('L2', 'dSkill7', [
            ('SkillCooldown', 'operation: 1\ntime: 108'), ('SkillRange', 'operation: 0\nrange: 40'),
            ('SkillEffectForce', 'operation: 1\nvalueType: 0\nforce: 1.6')]), 'l72')
        put(hd + 'SkillsData/Skill9/Skill9_Level1.prefab', level('L1', 'dSkill9', [('SkillUnitsCount', 'operation: 2\nvalueType: 0\ncount: 1.5')]), 'l91')
        put(hd + 'SkillsData/Skill16/Skill16_Level1.prefab', level('L1', 'dSkill16', [('SkillEffectDuration', 'operation: 1\nvalueType: 0\ntime: 6')]), 'l161')
        for n, lv in (('Skill7', ['l71', 'l72']), ('Skill9', ['l91']), ('Skill16', ['l161'])):
            levels = '\n'.join(f'  - level: {i + 1}\n    skill: {{fileID: 99, guid: {g}, type: 3}}' for i, g in enumerate(lv))
            put(hd + n + '.asset', mono(11400000, 'Header', 'levels:\n' + levels))

    def tearDown(self):
        self.tmp.cleanup()

    def run_convert(self):
        out = os.path.join(self.tmp.name, 'skills.json')
        rep = os.path.join(self.tmp.name, 'coverage.md')
        self.assertEqual(convert.main(['--bw', self.tmp.name, '--out', out, '--report', rep, '--px-per-cell', '50']), 0)
        with open(out) as f, open(rep) as r:
            return {a['id']: a for a in json.load(f)['playerAbilities']}, r.read()

    def test_yaml_wrapped_flow_and_lists(self):
        d = unityyaml.parse_documents(RAGE)
        sk = d[10][1]
        self.assertEqual(sk['items'][0]['modificators'][0]['fileID'], '14')
        self.assertEqual(d[14][1]['attackCoefficient']['value'], '1.5')
        self.assertEqual(d[14][1]['speedCoefficient']['value'], '1.05')

    def test_levels_filters_and_effects(self):
        A, rep = self.run_convert()
        rage = A['skill7']
        self.assertEqual(rage['target'], 'point')
        self.assertEqual(rage['cooldown'], 70)                       # GameData value of hero2|skill3|cooldown
        self.assertEqual(rage['cost'], {'energy': 350})
        e = rage['effects'][0]
        self.assertEqual(e['type'], 'status')
        self.assertEqual(e['radius'], 5)                             # 250 px / 50
        self.assertEqual(e['affects'], 'own')
        self.assertEqual(e['duration'], 8)
        self.assertEqual(e['aspect'], 0.5)                           # BW skill ranges are ellipses (world.aspectRatio)
        self.assertEqual(e['status']['mods'][0], {'stat': 'damage', 'mul': 1.5})
        lv2 = rage['levels'][0]
        self.assertEqual(lv2['cooldown'], 108)
        self.assertEqual(lv2['effects'][0]['radius'], 5.8)           # range + 40 px
        self.assertEqual(lv2['effects'][0]['status']['mods'][1], {'stat': 'speed', 'mul': 1.6})   # effectForce -> speedCoefficient (as in BW)
        bomb = A['skill9']
        self.assertEqual(bomb['target'], 'structure')                # oneNearest points filter on a click
        self.assertEqual(bomb['effects'][0], {'type': 'garrisonKill', 'percent': 0.9, 'kind': 'structure', 'affects': 'enemies', 'shape': 'target'})
        self.assertEqual(bomb['effects'][1]['delay'], 1.5)
        mines = A['skill16']['effects'][0]
        self.assertEqual(mines['lifetime'], [5, 7])                  # duration upgrade -> selfDestructionTimeout 6 +- 1
        self.assertTrue(mines['origins']['consume'])
        self.assertEqual(mines['origins']['maxTargets'], 15)
        self.assertIn('| UnitsRage | exact |', rep)


if __name__ == '__main__':
    unittest.main()
