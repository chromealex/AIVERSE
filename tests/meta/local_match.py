#!/usr/bin/env python3
"""Deterministic local match issuance boundaries; no real credentials or network."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('local_match', ROOT/'tools/meta/local_match.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def denied(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('expected rejection')

with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'meta.sqlite'
    db=sqlite3.connect(path)
    db.execute('CREATE TABLE sessions(hash TEXT PRIMARY KEY, player TEXT, authority INTEGER, expires INTEGER)')
    for token,player,authority,expires in [('alice','alice',0,2000),('bob','bob',0,2000),('admin','root',1,2000),('expired','old',0,1)]:
        db.execute('INSERT INTO sessions VALUES(?,?,?,?)',(hashlib.sha256(token.encode()).hexdigest(),player,authority,expires))
    db.commit();db.close();db=module.database(path)
    for token in ['unknown','admin','expired']:
        denied(lambda:module.issue(db,token,'match',1,100,1000))
    first=module.issue(db,'alice','match',1,100,1000)
    second=module.issue(db,'alice','match',1,100,1001)
    assert first['slot']==second['slot']==1 and first['token']!=second['token']
    denied(lambda:module.issue(db,'bob','match',1,100,1200)) # Expired ticket cannot free identity.
    denied(lambda:module.issue(db,'alice','match',2,100,1001))
    denied(lambda:module.issue(db,'alice','match',1,901,1001))
    out=Path(directory)/'tickets.json';module.export(db,out,1001)
    data=json.loads(out.read_text());assert len(data['tickets'])==1 and data['tickets'][0]['token']==second['token']
    assert out.stat().st_mode&0o777==0o600 and path.stat().st_mode&0o777==0o600
    db.execute('DELETE FROM local_match_tickets');db.commit();module.export(db,out,1001)
    assert json.loads(out.read_text())['tickets']==[]
    assert module.issue(db,'alice','match',1,100,1002)['slot']==1
    module.export(db,out,1200);assert json.loads(out.read_text())['tickets']==[]
    db.close()
print('Local match issuance: identity, rotation, capacity, expiry, revocation, private export OK')
