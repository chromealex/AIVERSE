#!/usr/bin/env python3
"""Exercise the real loopback listener; preserve a local stand and a credential-free report."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import sqlite3
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--requests', type=int, default=100)
    args = parser.parse_args()
    if not 1 <= args.requests <= 1000:
        parser.error('requests must be 1..1000')
    args.work_dir.mkdir(parents=True, exist_ok=False)
    config = args.work_dir/'service.json'
    config.write_text((ROOT/'examples/meta/service.json').read_text())
    db = args.work_dir/'meta.sqlite'
    tokens = {}
    for player in ('alice', 'bob', 'authority'):
        command = [sys.executable, str(ROOT/'tools/meta/server.py'), '--db', str(db), 'user', 'alice' if player == 'authority' else player]
        if player == 'authority':
            command.append('--authority')
        tokens[player] = subprocess.check_output(command, text=True).strip()
    # Keep only the normal player credential for the user; never write authority credentials.
    token_path = args.work_dir/'player-token.txt'
    fd = os.open(token_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as handle:
        handle.write(tokens['alice']+'\n')
    process = None
    base = None
    report = {'transport': 'HTTP loopback', 'checks': [], 'requests': args.requests}

    def start():
        nonlocal process, base
        process = subprocess.Popen([sys.executable, str(ROOT/'tools/meta/server.py'), '--db', str(db), 'serve', '--binary', str(args.binary.resolve()), '--config', str(config), '--port', '0'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        # The reference host flushes this only after binding its listener.
        ready = process.stdout.readline().strip()
        if not ready.startswith('Meta reference server: http://127.0.0.1:'):
            raise RuntimeError('listener failed: '+ready+' '+process.stderr.read())
        base = ready.split('server: ', 1)[1]

    def stop():
        nonlocal process
        if process:
            process.terminate()
            process.wait(timeout=10)
            process = None

    def rpc(player, service, action, arguments=None, operation=None, **extra):
        origin = extra.pop('origin', None)
        data = dict(service=service, action=action, arguments=arguments or {}, operation=operation or secrets.token_hex(12), **extra)
        headers = {'Content-Type': 'application/json', 'Authorization': 'Bearer '+tokens.get(player, 'invalid')}
        if origin:
            headers['Origin'] = origin
        request = urllib.request.Request(base+'/rpc', json.dumps(data).encode(), headers)
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    try:
        start()
        assert rpc('unknown', 'meta', 'view')[0] == 401
        assert rpc('alice', 'profile', 'create', origin='https://invalid.example')[0] == 403
        assert rpc('alice', 'profile', 'create', authority=True)[0] == 400
        report['checks'].append('unknown token, cross-origin and forged authority rejected')
        for player in ('alice', 'bob'):
            assert rpc(player, 'profile', 'create')[1]['code'] == 0
        assert rpc('alice', 'economy', 'grant', {'reward': {'gold': 1}})[1]['code'] == 8
        assert rpc('authority', 'economy', 'grant', {'reward': {'gold': 1}}, 'local-grant')[1]['code'] == 0
        assert rpc('authority', 'economy', 'grant', {'reward': {'gold': 1}}, 'local-grant')[1]['code'] == 1
        assert rpc('authority', 'economy', 'grant', {'reward': {'gold': 2}}, 'local-grant')[1]['code'] == 7
        view = rpc('alice', 'meta', 'view')[1]
        assert 'state' not in view and 'receipts' not in view['data']['profile']
        assert view['data']['profile']['balances']['gold'] == 501
        assert rpc('bob', 'meta', 'view')[1]['data']['profile']['balances']['gold'] == 500
        report['checks'].append('grant authorization, idempotent retry, conflicting retry and private profile separation')
        # Send and consume an unknown-length portion of a response, then disconnect.
        # The next request uses the same operation ID, as after a lost reply on a real link.
        body = json.dumps(dict(service='economy', action='grant', arguments={'reward': {'gold': 2}}, operation='lost-reply')).encode()
        port = int(base.rsplit(':', 1)[1])
        with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
            head = ('POST /rpc HTTP/1.1\r\nHost: 127.0.0.1:'+str(port)+'\r\nContent-Type: application/json\r\nAuthorization: Bearer '+tokens['authority']+'\r\nContent-Length: '+str(len(body))+'\r\nConnection: close\r\n\r\n').encode()
            sock.sendall(head+body)
            assert sock.recv(1)  # native transaction has completed before response headers
        assert rpc('authority', 'economy', 'grant', {'reward': {'gold': 2}}, 'lost-reply')[1]['code'] == 1
        assert rpc('alice', 'meta', 'view')[1]['data']['profile']['balances']['gold'] == 503
        report['checks'].append('disconnect after commit does not double-grant on retry')
        stop()
        start()
        assert rpc('alice', 'meta', 'view')[1]['data']['profile']['balances']['gold'] == 503
        assert rpc('authority', 'economy', 'grant', {'reward': {'gold': 1}}, 'local-grant')[1]['code'] == 1
        report['checks'].append('process restart preserves profile, session and operation receipts')

        def measured(_):
            before = time.perf_counter()
            status, reply = rpc('alice', 'meta', 'view')
            assert status == 200 and reply['code'] == 0
            return (time.perf_counter()-before)*1000

        begin = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            latency = sorted(pool.map(measured, range(args.requests)))
        elapsed = time.perf_counter()-begin
        report.update(concurrency=4, elapsed_seconds=elapsed, requests_per_second=args.requests/elapsed, p50_ms=latency[len(latency)//2], p95_ms=latency[min(len(latency)-1, int(len(latency)*.95))], maximum_ms=latency[-1])
        with sqlite3.connect(db) as connection:
            connection.execute('DELETE FROM sessions WHERE hash=?', (hashlib.sha256(tokens['bob'].encode()).hexdigest(),))
            connection.execute('DELETE FROM sessions WHERE authority=1')
        assert rpc('bob', 'meta', 'view')[0] == 401
        assert rpc('authority', 'meta', 'view')[0] == 401
        report['checks'].append('revocation takes effect; test authority credential removed')
        with urllib.request.urlopen(base+'/', timeout=10) as response:
            assert response.status == 200 and b'<!doctype html>' in response.read().lower()
        report['checks'].append('reference UI served over HTTP; rendering not checked')
    finally:
        stop()
    (args.work_dir/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    print('Local stand files:', args.work_dir)

if __name__ == '__main__':
    main()
