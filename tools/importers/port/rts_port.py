#!/usr/bin/env python3
"""rts_port SOURCE --out DEST; scan/extract/emit/report/item/resolve also emit JSON."""
import argparse
import json
import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lib.port.common import encoded, read_json
from lib.port.emit import emit, save_report


def importer(root):
    if (root/'Assets').is_dir() and (root/'ProjectSettings').is_dir():
        from unity_project import importer as module
    elif (root/'project.godot').is_file():
        from godot_project import importer as module
    elif (root/'unreal-export.json').is_file() or list(root.glob('*.uproject')):
        from unreal_export import importer as module
    else: raise ValueError('No Unity, Godot or Unreal project found in '+str(root))
    return module


def run(argv=None):
    argv=list(sys.argv[1:] if argv is None else argv)
    commands=('scan','extract','emit','import','report','item','resolve')
    if argv and argv[0] not in commands and not argv[0].startswith('-'): argv.insert(0,'import')
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command',choices=commands); ap.add_argument('path',type=Path)
    ap.add_argument('--out',type=Path); ap.add_argument('--scale',type=float,default=1)
    ap.add_argument('--mode',choices=('auto','2d','3d'),default='auto'); ap.add_argument('--mapping',type=Path); ap.add_argument('--id'); ap.add_argument('--how',choices=('rules','lua','native','dropped'))
    ap.add_argument('--reason'); args=ap.parse_args(argv)
    try:
        root=args.path.expanduser().resolve()
        if args.command=='scan': result=importer(root).scan(root)
        elif args.command=='extract':
            result=importer(root).extract(root)
            if args.out: args.out.write_bytes(encoded(result))
        elif args.command in ('import','emit'):
            if not args.out: raise ValueError('--out is required')
            model=read_json(root) if args.command=='emit' else importer(root).extract(root)
            result=emit(model,args.out.expanduser(),args.scale,args.mapping,args.mode)
        else:
            result=read_json(root/'port-report.json')
            if args.command in ('item','resolve'):
                entry=next((i for i in result['items'] if i['id']==args.id),None)
                if entry is None: raise ValueError('unknown report item id')
                if args.command=='resolve':
                    if not args.how or not args.reason: raise ValueError('--how and --reason are required')
                    entry.update(status='resolved',resolution={'how':args.how,'reason':args.reason})
                    save_report(root,result)
                    result=entry
                else:
                    model=read_json(root/'port.json')
                    result=dict(item=entry,entities=[e for e in model['entities'] if e['source']==entry['source']],
                                scripts=[s for s in model['scripts'] if s['path']==entry['source'] or any(e['source']==entry['source'] and s['path'] in e.get('scripts',[]) for e in model['entities'])],
                                generated={e['id']:read_json(root/'rules.json').get('types',{}).get(e['id']) for e in model['entities'] if e['source']==entry['source']})
        print(encoded(result).decode(),end='')
        return 0 if result.get('ok',True) else 2
    except (ValueError,OSError,KeyError,TypeError) as e:
        print(json.dumps({'ok':False,'error':str(e)},ensure_ascii=False)); return 2

if __name__=='__main__': sys.exit(run())
