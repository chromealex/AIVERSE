#!/usr/bin/env python3
"""Local meta identity -> actual FPS admission -> live revocation acceptance."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import socket
import sqlite3
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('local_match',ROOT/'tools/meta/local_match.py')
issuer=importlib.util.module_from_spec(spec);spec.loader.exec_module(issuer)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--server',type=Path,required=True)
parser.add_argument('--probe',type=Path,required=True)
args=parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);path=root/'meta.sqlite';out=root/'tickets.json';client=root/'client.txt'
    db=sqlite3.connect(path);db.execute('CREATE TABLE sessions(hash TEXT PRIMARY KEY, player TEXT, authority INTEGER, expires INTEGER)')
    db.execute('INSERT INTO sessions VALUES(?,?,?,?)',(hashlib.sha256(b'local-fixture').hexdigest(),'alice',0,int(time.time())+900));db.commit();db.close()
    db=issuer.database(path)
    record=issuer.issue(db,'local-fixture','acceptance',2,120,int(time.time()))
    issuer.private_write(client,record['token']+'\n');issuer.export(db,out,int(time.time()))
    # Reserve a candidate UDP port; the dedicated server also needs adjacent TCP.
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as candidate:
        candidate.bind(('127.0.0.1',0));port=candidate.getsockname()[1]
    command=[str(args.server.resolve()),'--project',str(ROOT/'examples/fps/arena'),'--port',str(port),'--loopback','--admission-file',str(out),'--audience','acceptance']
    with (root/'server.log').open('w+') as log:
        server=subprocess.Popen(command,stdout=log,stderr=log)
        try:
            deadline=time.monotonic()+10
            while True:
                if server.poll() is not None:raise RuntimeError('dedicated server exited')
                try:
                    with socket.create_connection(('127.0.0.1',port+1),timeout=.2):break
                except OSError:
                    if time.monotonic()>deadline:raise RuntimeError('dedicated server did not listen')
                    time.sleep(.05)
            probe=[str(args.probe.resolve()),str(ROOT/'examples/fps/arena'),f'udp://127.0.0.1:{port}',str(client)]
            subprocess.run(probe,check=True,timeout=15)
            time.sleep(.3)
            # A second connection resumes the same authoritative slot, then loses access.
            process=subprocess.Popen(probe+['--expect-revoke'])
            try:
                time.sleep(2)
                db.execute('DELETE FROM local_match_tickets');db.commit();issuer.export(db,out,int(time.time()))
                assert process.wait(timeout=10)==0
            finally:
                if process.poll() is None:process.kill();process.wait()
            print('Dedicated FPS: local profile admission, reconnect and live revocation passed')
        finally:
            server.terminate();server.wait(timeout=10)
    db.close()
