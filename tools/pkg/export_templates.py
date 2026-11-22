#!/usr/bin/env python3
"""Engine templates -> template packages of the catalog (AIVERSE-PACKAGES).

The editor's start screen takes its templates from the catalog: official ones (made by the AIVERSE
authors, marked "official" in the package) are listed first, the rest are found by search. This tool
turns templates/<id>/ of the engine into templates/<package-id>/ of a catalog checkout:

  aiverse.package.json   English title / summary with -ru translations, provides.engineTemplate = <id>
  README.md              what it is, how to start a project from it, licences
  preview.jpg            --previews <dir>/<id>.jpg (a real engine frame), else the template's branding art
  files/                 the template folder without its <Title>.aiverse (a new project writes its own)

  export_templates.py --catalog ../AIVERSE-PACKAGES [--previews shots/] [--only skirmish,mushrooms]
                      [--version 1.0.0]

files/ never reaches the catalog's main branch: the pull request is accepted by its owner and
tools/catalog.py accept turns it into the release archive. The engine keeps templates/ as the offline
fallback and as test data (determinism baselines, CI); a template that grows heavy content keeps it in
its package only. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from aiverse_pkg import strip_comments  # noqa: E402

MIN_ENGINE = '3.1.0'
SKIP_TEMPLATES = {'_tools', '_wizard', 'lua_lab'}   # build scripts, wizard kit, a Lua test lab
SKIP_TOP = ('.aiverse', '.rtsproj')                 # project files: a new project writes its own
SKIP_DIRS = {'Builds', '.temp', '__pycache__'}

# English text first; -ru from the template's own description where it had one.
TEXT = {
    'ambersands': ('In the style of Dune II: spice harvesting, windtraps and a sandworm in the dunes.',
                   'Как Dune II: спайс, ветряки, червь в песках.', ['rts', 'desert', 'base-building']),
    'borderrush': ('In the style of State.io: troops leave the bases on their own and capture cells.',
                   'Как State.io: войска сами захватывают клетки.', ['capture', 'casual']),
    'boss_arena': ('A top-down hero against a three-phase golem boss with adds and crawler packs: telegraphed area '
                   'attacks, statuses and boss phases.',
                   'Герой с видом сверху против трёхфазного босса-голема с прислужниками и стаями ползунов: атаки '
                   'по площади с предупреждением, статусы и фазы босса.', ['boss', 'top-down', 'encounters']),
    'clanlands': ('In the style of Northgard: capture regions and pay for them with food.',
                  'Как Northgard: захват регионов за еду.', ['rts', 'regions', 'capture']),
    'extraction': ('Extraction raid: containers, raid inventory, objectives, extraction points and receipts. '
                   'Two maps: PvE and PvP with two raiders.',
                   'Рейд с эвакуацией: контейнеры, инвентарь рейда, задачи, точки эвакуации и итог рейда. '
                   'Две карты: PvE и PvP на двоих.', ['extraction', 'raid', 'loot']),
    'flagsectors': ('In the style of Z: sectors with flags and factories.',
                    'Как Z: сектора с флагами и фабриками.', ['rts', 'sectors', 'capture']),
    'fourages': ('In the style of Age of Empires II: four resources and ages.',
                 'Как Age of Empires II: 4 ресурса и эпохи.', ['rts', 'ages', 'economy']),
    'fps_arena': ('A 5 vs 5 team arena: cover, bots and a network match.',
                  'Командная арена 5 на 5: укрытия, боты и сетевой матч.', ['fps', 'arena', 'bots']),
    'fps_survival': ('A large map for 100 players: streamed environment, drop from a plane, a phased zone, '
                     'a red zone and airdrops.',
                     'Большая карта, 100 мест, потоковая загрузка окружения, высадка с самолёта, зона по фазам, '
                     'красная зона и аирдропы.', ['fps', 'battle-royale', 'large-world']),
    'fps_tactical': ('Tactical rounds: buy weapons, plant and defuse the bomb.',
                     'Тактические раунды: покупка оружия, установка и разминирование бомбы.',
                     ['fps', 'tactical', 'rounds']),
    'fps_training': ('A shooting range: targets, shooting, reloading and an obstacle course.',
                     'Тренировочный тир: мишени, стрельба, перезарядка и полоса препятствий.',
                     ['fps', 'training', 'range']),
    'frostcrown': ('In the style of Warcraft III: heroes, creeps and terrain.',
                   'Как Warcraft III: герои, крипы, рельеф.', ['rts', 'heroes', 'terrain']),
    'hearthhaul': ('In the style of Knights and Merchants: goods carried along roads.',
                   'Как Knights and Merchants: товары и дороги.', ['economy', 'logistics', 'roads']),
    'holdtheline': ('In the style of Company of Heroes: squads and capture points.',
                    'Как Company of Heroes: отряды и точки.', ['rts', 'squads', 'capture-points']),
    'ironfront': ('In the style of Red Alert: MCV, ore and a sidebar.',
                  'Как Red Alert: MCV, руда, сайдбар.', ['rts', 'base-building', 'sidebar']),
    'km2d': ('2D sprites on 3D terrain, in the style of Knights and Merchants.',
             '2D-спрайты на 3D-рельефе (K&M).', ['2d', 'sprites', 'terrain']),
    'laneclash': ('A lane battler: spawners and waves.',
                  'Лайн-баттлер: спавнеры и волны.', ['lanes', 'waves']),
    'lastcolony': ('In the style of They Are Billions: survive the waves. A second map has huge hordes on flow fields.',
                   'Как They Are Billions: выжить в волнах. На второй карте огромные орды на потоковых полях.',
                   ['survival', 'waves', 'hordes']),
    'metaltide': ('In the style of Total Annihilation: flow economy, radar, reclaim, aircraft and nukes.',
                  'Как Total Annihilation: потоки ресурсов, радар, переработка, авиация и ядерные ракеты.',
                  ['rts', 'flow-economy']),
    'mushrooms': ('In the style of Mushroom Wars: garrisons and roads.',
                  'Как Mushroom Wars: гарнизоны и дороги.', ['capture', 'garrisons']),
    'perk_arena': ('A hero arena with in-match progression: experience orbs, level-up perk offers, summons and a '
                   'battle deck.',
                   'Арена героя с прокачкой в матче: шары опыта, выбор перков при повышении уровня, призывы и '
                   'боевая колода.', ['perks', 'progression', 'top-down']),
    'planets': ('A planet and a moon, orbits and flights between them. A second map is a big planet with flow fields.',
                'Планета и луна, орбиты, перелёты. Вторая карта — большая планета с потоковыми полями.',
                ['planets', 'orbits']),
    'racing': ('A race on an oval: starting grid, countdown, three laps against three bots, positions, wrong-way '
               'detection and returning to the track.',
               'Гонка на овале: сетка старта, отсчёт, три круга против трёх ботов, позиции, езда не в ту сторону '
               'и возврат на трассу.', ['racing', 'vehicles', 'bots']),
    'road_race': ('A race on a spline road circuit with four trucks whose wheels roll and steer.',
                  'Гонка по кольцу из сплайновой дороги: четыре грузовика, колёса катятся и поворачивают.',
                  ['racing', 'roads', 'vehicles']),
    'scripted': ('Iron and Gold with Lua: a war chest, veterans, a scripted chain heal and a custom defeat condition.',
                 'Iron and Gold на Lua: казна, ветераны, цепное лечение скриптом и своё условие поражения.',
                 ['lua', 'scripting', 'rts']),
    'showcase': ('Iron and Gold with 3D models and animation.',
                 'Iron and Gold с 3D-моделями и анимацией.', ['models', 'animation', 'rts']),
    'shroomwars': ('In the style of Mushroom Wars 2: levels, forges, morale and heroes.',
                   'Mushroom Wars 2: уровни, кузницы, мораль, герои.', ['capture', 'garrisons', 'heroes']),
    'skirmish': ('Classic base building in the style of Warcraft and Red Alert: base, army, towers.',
                 'Классика Warcraft/RA: база, армия, башни.', ['rts', 'base-building']),
    'spellbunkers': ('Spore Lanes with a spell book, in the style of Bunker Wars.',
                     'Spore Lanes + книга заклинаний (Bunker Wars).', ['capture', 'spells']),
    'sporelanes': ('In the style of Mushroom Wars 2: columns along lanes.',
                   'Как Mushroom Wars 2: колонны по линиям.', ['capture', 'lanes']),
    'starfall': ('In the style of StarCraft: three races that build in different ways.',
                 'Как StarCraft: три расы, разные стройки.', ['rts', 'races']),
    'tiles2d': ('2D tiles in the style of Dune II and Red Alert.',
                '2D-тайлы как Dune II / Red Alert.', ['2d', 'tiles']),
    'topdown_arena': ('A 1 vs 1 top-down hero arena: twin-stick control, virtual joystick and buttons, follow camera.',
                      'Арена героев с видом сверху 1 на 1: управление двумя стиками, виртуальный джойстик и кнопки, '
                      'камера следования.', ['top-down', 'heroes', 'mobile']),
    'underlord': ('In the style of Dungeon Keeper: digging, rooms and monsters.',
                  'Как Dungeon Keeper: копать, комнаты, монстры.', ['dungeon', 'digging']),
    'voxel_sandbox': ('A block sandbox: dig, build and survive the night. Endless world, fluids, mobs, network play.',
                      'Песочница из блоков: копайте, стройте, переживите ночь. Бесконечный мир, жидкости, мобы, '
                      'сетевая игра.', ['voxel', 'sandbox', 'survival']),
    'wildfire': ('Grid fields: forest fires and spreading creep.',
                 'Поля на сетке: лесные пожары и расползающаяся слизь.', ['fields', 'fire']),
}


def package_id(template: str) -> str:
    return template.replace('_', '-')


def load_game(folder: Path) -> dict:
    return json.loads(strip_comments((folder / 'game.json').read_text(encoding='utf-8')))


def font_notes(folder: Path) -> list[str]:
    """Copyright lines of fonts in the template (they are OFL, not MIT)."""
    out = []
    for f in sorted(folder.rglob('*.[ot]tf')):
        data = f.read_bytes()
        text = data.decode('latin-1')
        m = re.search(r'Copyright \(c\)[^\x00]{4,160}', text) or re.search(r'C\x00o\x00p\x00y\x00r\x00i\x00g\x00h\x00t[^\n]{0,320}', text)
        line = m.group(0).replace('\x00', '') if m else 'see the font file'
        lic = 'SIL Open Font License 1.1' if 'Open Font License' in text.replace('\x00', '') else 'see the font file'
        out.append(f'`{f.relative_to(folder).as_posix()}`: {line.strip()} — {lic}.')
    return out


def preview_for(tid: str, folder: Path, shots: Path | None) -> Path | None:
    if shots:
        for ext in ('.jpg', '.png'):
            if (shots / f'{tid}{ext}').is_file():
                return shots / f'{tid}{ext}'
    for c in ('branding/splash_bg.png', 'branding/studio.png', 'preview.png'):
        if (folder / c).is_file():
            return folder / c
    return None


def export(tid: str, out_root: Path, shots: Path | None, version: str) -> str:
    src = ENGINE / 'templates' / tid
    game = load_game(src)
    en, ru, tags = TEXT[tid]
    pid = package_id(tid)
    dest = out_root / 'templates' / pid
    if dest.exists():
        shutil.rmtree(dest)
    (dest / 'files').mkdir(parents=True)
    for p in sorted(src.rglob('*')):
        rel = p.relative_to(src)
        if any(part in SKIP_DIRS for part in rel.parts):
            continue
        if len(rel.parts) == 1 and p.suffix in SKIP_TOP:
            continue
        if p.is_dir():
            (dest / 'files' / rel).mkdir(parents=True, exist_ok=True)
        else:
            (dest / 'files' / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dest / 'files' / rel)
    maps = [m.get('title') or m.get('file') if isinstance(m, dict) else str(m) for m in game.get('maps', [])]
    title = game.get('name') or tid
    manifest = {
        'schema': 2, 'id': pid, 'version': version, 'kind': 'template', 'category': 'templates',
        'title': title, 'summary': en, 'summary-ru': ru,
        'license': 'MIT', 'minEngine': MIN_ENGINE, 'official': True,
        'authors': ['AIVERSE'], 'homepage': 'https://github.com/chromealex/AIVERSE',
        'tags': ['template', *tags],
        'provides': {'engineTemplate': tid, 'module': game.get('module', 'game'), 'maps': maps},
    }
    pv = preview_for(tid, src, shots)
    if pv:
        shutil.copyfile(pv, dest / f'preview{pv.suffix.lower()}')
        manifest['previews'] = [f'preview{pv.suffix.lower()}']
    (dest / 'aiverse.package.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n',
                                               encoding='utf-8')
    fonts = font_notes(src)
    readme = [f'# {title}', '', en, '', ru, '',
              f'Official AIVERSE project template (engine folder `templates/{tid}`).', '',
              '## Start a project from it', '',
              'In AIVERSE Studio: the start screen lists the official templates; pick it and press **Create**.',
              'From a shell:', '', '```sh', f'python3 tools/pkg/aiverse_pkg.py create {pid} ~/AiverseProjects/MyGame',
              '```', '', '## Maps', '', *[f'- {m}' for m in maps], '',
              '## Licence', '', 'MIT, like the engine (Copyright (c) 2026 Alex Silaev).']
    if fonts:
        readme += ['', 'Fonts keep their own licence:', '', *[f'- {n}' for n in fonts]]
    if (src / 'branding' / 'LICENSE.txt').is_file():
        readme += ['', 'Branding art: ' + (src / 'branding' / 'LICENSE.txt').read_text(encoding='utf-8').strip()]
    (dest / 'README.md').write_text('\n'.join(readme) + '\n', encoding='utf-8')
    return pid


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--catalog', type=Path, required=True, help='AIVERSE-PACKAGES checkout')
    ap.add_argument('--previews', type=Path, help='folder with <template>.jpg engine frames')
    ap.add_argument('--only', help='comma-separated engine template ids')
    ap.add_argument('--version', default='1.0.0')
    a = ap.parse_args(argv)
    ids = a.only.split(',') if a.only else sorted(
        p.name for p in (ENGINE / 'templates').iterdir()
        if p.is_dir() and p.name not in SKIP_TEMPLATES and (p / 'game.json').is_file())
    missing = [t for t in ids if t not in TEXT]
    if missing:
        print('no catalog text for: ' + ', '.join(missing), file=sys.stderr)
        return 2
    for t in ids:
        print(f'{t} -> templates/{export(t, a.catalog, a.previews, a.version)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
