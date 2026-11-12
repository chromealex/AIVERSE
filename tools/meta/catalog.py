#!/usr/bin/env python3
"""Discover meta packages and pin their implementation + recipes for native projects."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = '0.5.0'
PACKAGES = [
    ('server', 'authoritative server policies permissions transactions service modules', 'meta-server', 'engine/meta/Service.h'),
    ('database', 'server SQLite schema migrations sessions atomic snapshot CAS', 'meta-database', 'tools/meta/db/001_initial.sql'),
    ('client', 'native host UI durable outbox retry restart backend requests', 'meta-client', 'engine/meta/Client.h'),
    ('economy', 'shop offers currencies crafting rewards payments', 'meta-economy', 'engine/meta/Meta.h'),
    ('progression', 'quests achievements pass daily energy inbox', 'meta-progression', 'engine/meta/Meta.h'),
    ('social', 'leaderboards ranking chat moderation', 'meta-social', 'engine/meta/Social.h'),
    ('clans', 'friends requests clan roles donations goals', 'meta-clans', 'engine/meta/Service.h'),
    ('items', 'collection instance equipment upgrade merge salvage', 'meta-items', 'engine/meta/Service.h'),
    ('tournaments', 'tournament league cohorts seasons promotion rewards', 'meta-tournaments', 'engine/meta/Service.h'),
    ('backend', 'backend HTTP SQLite authentication persistence entitlements subscription native transport', 'meta-backend', 'engine/meta/Service.h'),
    ('loot', 'nested chests weighted rewards pity duplicate protection', 'meta-loot', 'engine/meta/LiveOps.h'),
    ('albums', 'seasonal collections albums duplicate trades', 'meta-albums', 'engine/meta/LiveOps.h'),
    ('jobs', 'crafting construction queues timed slots speedups cancellation', 'meta-jobs', 'engine/meta/LiveOps.h'),
    ('research', 'research trees dependencies exclusive branches prestige', 'meta-research', 'engine/meta/LiveOps.h'),
    ('quests', 'compound quests all any goals daily variants reroll branching', 'meta-quests', 'engine/meta/LiveOps.h'),
    ('events', 'global personal events phases late joins catchup event currencies', 'meta-events', 'engine/meta/LiveOps.h'),
    ('wars', 'clan wars roster pairing contributions settlement rewards', 'meta-wars', 'engine/meta/LiveOps.h'),
    ('async-pvp', 'asynchronous PvP defense snapshots raids revenge shields', 'meta-async-pvp', 'engine/meta/LiveOps.h'),
    ('matchmaking', 'party matchmaking regions widening queues backfill leave penalties', 'meta-matchmaking', 'engine/meta/LiveOps.h'),
    ('ratings', 'Elo Glicko2 ranked periods provisional teams streaks leavers', 'meta-ratings', 'engine/meta/LiveOps.h'),
    ('referrals', 'referral invites milestone rewards promo codes', 'meta-referrals', 'engine/meta/LiveOps.h'),
    ('ads', 'rewarded ads tickets caps cooldown server callbacks', 'meta-ads', 'engine/meta/LiveOps.h'),
    ('offer-chains', 'offer chains accept dismiss transitions impression frequency', 'meta-offer-chains', 'engine/meta/LiveOps.h'),
    ('notifications', 'notification badges grouped priorities reminders read state', 'meta-notifications', 'engine/meta/LiveOps.h'),
    ('offline', 'offline income capped accrual rate tiers checkpoints', 'meta-offline', 'engine/meta/LiveOps.h'),
    ('ui', 'screens shop chat leaderboard inbox UI themes localization', 'meta-ui', 'examples/meta/ui/app.js'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['list', 'search', 'describe', 'check', 'pin', 'verify'])
    parser.add_argument('query', nargs='?', default='')
    parser.add_argument('--project', type=Path)
    parser.add_argument('--section', default='', help='dot path within describe example; reduces output')
    args = parser.parse_args()
    entries = [dict(id=k, version=VERSION, summary=summary, skill=f'skills/{skill}/SKILL.md', source=source)
               for k, summary, skill, source in PACKAGES]
    if args.command in ('list', 'search'):
        print(json.dumps([p for p in entries if args.query.lower() in json.dumps(p).lower()], indent=2))
        return
    if args.command == 'describe':
        services = {'loot': 'loot', 'albums': 'albums', 'jobs': 'jobs', 'research': 'research', 'quests': 'quests', 'events': 'events', 'wars': 'wars', 'async-pvp': 'asyncPvp', 'matchmaking': 'matchmaking', 'ratings': 'ratings', 'referrals': 'referrals', 'ads': 'ads', 'offer-chains': 'chains', 'notifications': 'notifications', 'offline': 'offline'}
        package = next((p for p in entries if p['id']==args.query), None)
        if not package:
            parser.error('unknown package')
        if args.query in services:
            service = services[args.query]
            schema = json.loads((ROOT/'tools/meta/liveops.schema.json').read_text())
            examples = json.loads((ROOT/'examples/meta/liveops.json').read_text())
            result = dict(package, service=service, schema=schema['properties'][service], example=examples[service])
        else:
            from kits import definitions, resolve
            config=definitions(resolve([args.query]))
            if args.query=='economy': example=config['economy']
            elif args.query=='progression': example={k:config['economy'][k] for k in ('tracks','daily','energy')}
            elif args.query=='social': example={k:config[k] for k in ('boards','chats')}
            elif args.query in ('clans','items','tournaments'): example=config['extensions'][args.query]
            elif args.query=='ui': example=config['economy']['metadata']
            else: example={'service':'meta','action':'view','arguments':{}}
            result=dict(package,example=example,cmakeTarget=('AIVERSE::meta_client' if args.query=='client' else None if args.query in ('ui','database') else 'AIVERSE::meta_server'),nativeHelper='aiverse_native_use_meta')
        if args.section:
            try:
                value=result['example']
                for key in args.section.split('.'): value=value[key]
                result={k:v for k,v in result.items() if k not in ('schema','example')}
                result['example']=value
            except (KeyError,TypeError):parser.error('unknown example section')
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return
    for package in entries:
        assert (ROOT/package['source']).is_file(), package['source']
        skill = (ROOT/package['skill']).read_text()
        assert skill.startswith('---\nname: ') and '\ndescription: ' in skill, package['skill']
        for line in skill.splitlines():
            assert not line.startswith('TODO'), package['skill']
    if args.command == 'check':
        print(f'Validated {len(entries)} meta packages and project recipes')
        return
    if not args.project or not (args.project/'meta/economy.json').is_file():
        parser.error('--project must contain meta/economy.json')
    files = set()
    for root in ['engine/meta', 'tools/meta', 'examples/meta/ui']:
        files.update(p for p in (ROOT/root).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    files.update(ROOT/p['skill'] for p in entries)
    files.update([ROOT/'engine/data/Json.h', ROOT/'engine/data/Json.cpp', ROOT/'engine/core/Fixed.h', ROOT/'engine/core/Fixed.cpp', ROOT/'docs/meta-liveops.md', ROOT/'examples/meta/liveops.json', ROOT/'libraries/include/aiverselib/Progression.h', ROOT/'libraries/include/aiverselib/systems/Loot.h', ROOT/'libraries/include/aiverselib/Graphs.h', ROOT/'docs/meta-service.md', ROOT/'docs/meta-game.md', ROOT/'skills/meta-game/SKILL.md', ROOT/'skills/meta-game/references/catalog.md'])
    pin = {'version': VERSION, 'files': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for p in sorted(files)}}
    destination = args.project/'meta/package.lock.json'
    old = json.loads(destination.read_text()) if destination.exists() else None
    if args.command == 'verify':
        if old != pin:
            parser.error('missing or changed pin; review upgrade/migration explicitly')
        print('Meta package pin verified')
    else:
        if old is not None and old != pin:
            parser.error('pin differs; review and remove old lock explicitly before upgrading')
        if old is None:
            with destination.open('x') as f:
                json.dump(pin, f, indent=2, sort_keys=True)
                f.write('\n')
        print('Meta package pinned; link aiverse::meta')


if __name__ == '__main__':
    main()
