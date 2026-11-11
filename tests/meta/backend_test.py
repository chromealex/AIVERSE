"""Exercise HTTP parser/handler through memory sockets, native dispatch and SQLite restarts.
No listening socket is required; network/TLS must be smoke-tested by the deployer.
"""
import importlib.util
import json
import io
import pathlib
import secrets
import sqlite3
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import HTTPServer

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('meta_server', ROOT/'tools/meta/server.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
binary = pathlib.Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix='rts-meta-http-') as tmp:
    path = pathlib.Path(tmp)/'meta.sqlite'
    config_path = pathlib.Path(tmp)/'service.json'
    config = json.loads((ROOT/'examples/meta/service.json').read_text())
    config['extensions']['liveops'] = json.loads((ROOT/'examples/meta/liveops.json').read_text())
    config_path.write_text(json.dumps(config))
    db = module.database(path)
    tokens = {p: secrets.token_urlsafe(32) for p in ('alice', 'bob', 'admin')}
    for p, token in tokens.items():
        db.execute('INSERT INTO sessions VALUES(?,?,?,?)', (module.token_hash(token), 'alice' if p == 'admin' else p, p == 'admin', int(time.time())+60))
    db.commit()
    db.close()
    def start():
        return module.Host(module.database(path), binary, config_path)

    def call(p, service, action, arguments=None, operation=None, **extra):
        origin = extra.pop('_origin', None)
        method = extra.pop('_method', 'POST')
        route = extra.pop('_path', '/rpc')
        body = json.dumps(dict(service=service, action=action, arguments=arguments or {}, operation=operation or secrets.token_hex(10), **extra)).encode()
        class Connection:
            def __init__(self):
                origin_header = f'Origin: {origin}\r\n' if origin else ''
                headers = (f'{method} {route} HTTP/1.1\r\nHost: localhost\r\n{origin_header}Content-Type: application/json\r\nAuthorization: Bearer {tokens.get(p, "invalid")}\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n').encode()
                self.input = io.BytesIO(headers+body)
                self.output = bytearray()
            def makefile(self, *args):
                return self.input
            def sendall(self, data):
                self.output.extend(data)
        connection = Connection()
        module.handler(thread)(connection, ('127.0.0.1', 1234), None)
        head, data = bytes(connection.output).split(b'\r\n\r\n', 1)
        return int(head.split(b' ')[1]), json.loads(data)

    thread = start()
    try:
        assert call('alice', 'profile', 'create', _origin='https://evil.example')[0] == 403
        thread.public_origin = 'https://game.example'
        assert call('alice', 'profile', 'create', _origin='https://game.example')[1]['code'] == 0
        thread.public_origin = None
        assert call('unknown', 'profile', 'create')[0] == 401
        for p in ('alice', 'bob'):
            assert call(p, 'profile', 'create')[1]['code'] in (0, 1)
        assert call('alice', 'economy', 'grant', {'reward': {'gold': 1}})[1]['code'] == 8
        assert call('alice', 'economy', 'grant', {'reward': {'gold': 1}}, authority=True)[0] == 400
        assert call('admin', 'economy', 'grant', {'reward': {'gold': 1}}, 'grant')[1]['code'] == 0
        assert call('admin', 'economy', 'grant', {'reward': {'gold': 1}}, 'grant')[1]['code'] == 1
        assert call('admin', 'economy', 'grant', {'reward': {'gold': 2}}, 'grant')[1]['code'] == 7
        assert call('alice', 'chat', 'send', {'id': 'global', 'text': 'hello'})[1]['code'] == 0
        view = call('bob', 'meta', 'view')[1]
        assert view['data']['chats']['global'][0]['text'] == 'hello'
        assert 'state' not in view and 'receipts' not in view['data']['profile']
        assert call('alice', 'friends', 'request', {'target': 'bob'})[1]['code'] == 0
        assert call('bob', 'friends', 'accept', {'target': 'alice'})[1]['code'] == 0
        before = call('alice', 'meta', 'view')[1]['data']
        assert before['profile']['balances']['gold'] == 501
        catalog = call('alice', '', '', _method='GET', _path='/catalog')[1]
        assert 'codes' not in catalog['extensions']['liveops']['referrals']['friends']
        assert thread.definition['extensions']['liveops']['referrals']['friends']['codes']
        ticket = call('alice', 'ads', 'begin', {'id': 'bonus'})[1]['data']['ticket']
        callback = {'id': 'bonus', 'ticket': ticket, 'token': 'verified-provider-token'}
        assert call('alice', 'ads', 'complete', callback)[1]['code'] == 8
        assert call('admin', 'ads', 'complete', callback, 'ad-complete')[1]['code'] == 0
        assert call('admin', 'ads', 'complete', callback, 'ad-complete')[1]['code'] == 1
        opened = call('alice', 'loot', 'open', {'id': 'chest'}, 'chest-open')[1]
        assert opened['code'] == 0
        retry = call('alice', 'loot', 'open', {'id': 'chest'}, 'chest-open')[1]
        assert retry['code'] == 1 and retry['data'] == opened['data']
        before = call('alice', 'meta', 'view')[1]['data']

    finally:
        thread.db.close()
    thread = start()
    try:
        after = call('alice', 'meta', 'view')[1]['data']
        assert after == before, (after, before)
        assert call('admin', 'economy', 'grant', {'reward': {'gold': 1}}, 'grant')[1]['code'] == 1
        db = module.database(path)
        db.execute('DELETE FROM sessions WHERE player=?', ('alice',))
        db.commit()
        db.close()
        assert call('alice', 'meta', 'view')[0] == 401
    finally:
        thread.db.close()
print('HTTP handler, authentication, native dispatch, SQLite and restart tests passed (memory sockets)')
