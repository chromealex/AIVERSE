#!/usr/bin/env python3
"""Reference validation, desync trace comparison and explicit library upgrade planning."""
import argparse
import hashlib
import json
from pathlib import Path

from gameplay import read_json as read
def select(value,path):
    if not path.startswith('/'):raise ValueError('selector must start with /')
    nodes=[('',value)]
    for part in path[1:].split('/'):
        part=part.replace('~1','/').replace('~0','~');following=[]
        for prefix,node in nodes:
            if part=='*':
                values=node.items() if isinstance(node,dict) else enumerate(node) if isinstance(node,list) else []
                following.extend((prefix+'/'+str(k),v) for k,v in values)
            elif part=='@keys':
                if not isinstance(node,dict):raise ValueError('@keys requires object: '+prefix)
                following.extend((prefix+'/'+k,k) for k in node)
            elif isinstance(node,dict) and part in node:following.append((prefix+'/'+part,node[part]))
            elif isinstance(node,list) and part.isdigit() and int(part)<len(node):following.append((prefix+'/'+part,node[int(part)]))
        nodes=following
    return nodes

def reference_report(document,spec,root):
    errors=[];checked=0
    for rule in spec:
        if not isinstance(rule,dict) or set(rule)-{'source','target','kind','required','ignore'}:raise ValueError('invalid reference rule')
        kind=rule.get('kind','id')
        if kind not in ('id','file'):raise ValueError('unknown reference kind')
        sources=select(document,rule['source'])
        if rule.get('required',False) and not sources:errors.append(dict(path=rule['source'],error='no source matches'))
        targets=select(document,rule['target']) if kind=='id' else []
        if any(not isinstance(v,str) for _,v in targets):raise ValueError('target IDs must be strings')
        allowed={v for _,v in targets}
        if len(allowed)!=len(targets):errors.append(dict(path=rule['target'],error='duplicate target identifiers'))
        for path,value in sources:
            if isinstance(value,str) and value in rule.get('ignore',[]):continue
            checked+=1
            if not isinstance(value,str) or not value:errors.append(dict(path=path,error='expected nonempty reference'));continue
            if kind=='id':
                if value not in allowed:errors.append(dict(path=path,error='unknown reference',value=value))
            else:
                file=(root/value).resolve()
                try:file.relative_to(root.resolve())
                except ValueError:errors.append(dict(path=path,error='path escapes project',value=value));continue
                if Path(value).is_absolute() or not file.is_file():errors.append(dict(path=path,error='missing or absolute file',value=value))
    return dict(ok=not errors,checked=checked,errors=errors)

def first_diff(a,b,path=''):
    if type(a)!=type(b):return dict(path=path,expected=a,actual=b)
    if isinstance(a,dict):
        for key in sorted(set(a)|set(b)):
            child=path+'/'+key.replace('~','~0').replace('/','~1')
            if key not in a or key not in b:return dict(path=child,expected=a.get(key),actual=b.get(key),missing='expected' if key not in a else 'actual')
            diff=first_diff(a[key],b[key],child)
            if diff:return diff
    elif isinstance(a,list):
        for i in range(max(len(a),len(b))):
            if i>=len(a) or i>=len(b):return dict(path=path+'/'+str(i),expected=a[i] if i<len(a) else None,actual=b[i] if i<len(b) else None)
            diff=first_diff(a[i],b[i],path+'/'+str(i))
            if diff:return diff
    elif a!=b:return dict(path=path,expected=a,actual=b)
    return None

def trace_diff(expected,actual):
    def validate(trace):
        last=-1
        for row in trace:
            if not isinstance(row,dict) or set(row)!={'tick','modules'} or type(row['tick']) is not int or row['tick']<=last or not isinstance(row['modules'],dict):raise ValueError('invalid trace; require increasing tick and modules object')
            last=row['tick']
    validate(expected);validate(actual)
    for index in range(max(len(expected),len(actual))):
        if index>=len(expected) or index>=len(actual):return dict(ok=False,error='trace length mismatch',index=index)
        a,b=expected[index],actual[index]
        if a['tick']!=b['tick']:return dict(ok=False,error='tick coverage mismatch',expected=a['tick'],actual=b['tick'])
        diff=first_diff(a['modules'],b['modules'])
        if diff:return dict(ok=False,tick=a['tick'],difference=diff)
    return dict(ok=True)

def upgrade_plan(lock,catalog,engine):
    if lock.get('schema')!=2:raise ValueError('expected lock schema 2')
    packages={p['id']:p for p in catalog['packages']};changes=[]
    for id,pin in sorted(lock['packages'].items()):
        if id not in packages:changes.append(dict(id=id,status='removed',action='manual replacement'));continue
        p=packages[id];files={};pending=[p['source'],'libraries/CMakeLists.txt',*p.get('dependencies',[])];seen=set()
        import re
        while pending:
            name=pending.pop();file=(engine/name).resolve()
            try:relative=file.relative_to(engine.resolve()).as_posix()
            except ValueError:raise ValueError('dependency outside engine')
            if relative in seen:continue
            seen.add(relative);data=file.read_bytes().replace(b'\r\n',b'\n');files[relative]=hashlib.sha256(data).hexdigest()
            if file.suffix=='.h':
                for include in re.findall(r'^#include "([^"]+)"',data.decode(),re.M):
                    child=file.parent/include
                    if child.is_file():pending.append(str(child))
        changed=pin.get('files')!=files or pin.get('version')!=p['version'] or pin.get('layer')!=p['layer']
        changes.append(dict(id=id,status='review' if changed else 'unchanged',oldVersion=pin.get('version'),newVersion=p['version'],changedFiles=sorted(k for k in set(pin.get('files',{}))|set(files) if pin.get('files',{}).get(k)!=files.get(k)),requiredChecks=['catalog','native-export','project-restore'] if changed else []))
    return dict(ok=True,reviewRequired=any(c['status']!='unchanged' for c in changes),packages=changes)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['references','desync','upgrade']);p.add_argument('--input',required=True,type=Path);p.add_argument('--other',required=True,type=Path);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--output',type=Path);a=p.parse_args()
    try:
        first,second=read(a.input),read(a.other)
        result=reference_report(first,second,a.root) if a.command=='references' else trace_diff(first,second) if a.command=='desync' else upgrade_plan(first,second,a.root)
        text=json.dumps(result,indent=2)+'\n'
        if a.output:
            if a.output.resolve() in (a.input.resolve(),a.other.resolve()):raise ValueError("report cannot overwrite its input")
            a.output.write_text(text)
        else:print(text,end='')
        return 0 if result['ok'] else 1
    except (ValueError,KeyError,TypeError,OSError) as e:p.error(str(e))
if __name__=='__main__':raise SystemExit(main())
