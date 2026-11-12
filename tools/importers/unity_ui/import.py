#!/usr/bin/env python3
"""CLI of the Unity UI importer (see importer.py)."""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importer  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=importer.__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, help='Unity project folder or its Assets folder')
    ap.add_argument('--out', required=True, help='game folder to write into')
    ap.add_argument('--screens', action='append', default=[], help='folder (relative to Assets) searched for UI.Windows screens')
    ap.add_argument('--prefab', action='append', default=[], help='a plain uGUI prefab to convert as a screen')
    ap.add_argument('--only', action='append', default=[], help='convert only these screens (name or prefab name)')
    ap.add_argument('--no-l10n', action='store_true')
    ap.add_argument('--no-screens', action='store_true')
    ap.add_argument('--no-copy', action='store_true', help='do not copy textures and fonts')
    ap.add_argument('--key-prefix', default='', help='prefix of the engine string keys (default: <Collection>.<Key>)')
    ap.add_argument('--json', action='store_true', help='print the whole report as JSON')
    a = ap.parse_args()
    res = importer.run(a.source, a.out, a.screens, a.prefab, a.only, not a.no_l10n, not a.no_screens, a.key_prefix, not a.no_copy)
    if a.json:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        for s in res['screens']:
            iss = s['issues']
            print('screen %-24s %2d blocks %3d controls  unsupported: %s' % (
                s['name'], len(s['blocks']), s['controls'], ', '.join('%s x%d' % (k, v['count']) for k, v in iss['unsupported'].items()) or '-'))
        if 'localization' in res:
            l = res['localization']
            print('languages: %s (source %s), files: %s' % (', '.join(l['languages']), l['sourceLanguage'], ', '.join(l['files'])))
        print('report: %s' % os.path.join(res['out'], 'ui', 'ugui', 'report.md'))
    return 0 if res['ok'] else 2


if __name__ == '__main__':
    sys.exit(main())
