#!/usr/bin/env python3
"""Reference HTTP/SQLite host for the C++ meta service. No game policies in Python."""
import copy
import argparse
import hashlib
import json
import secrets
import sqlite3
import subprocess
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]


def database(path):
    db = sqlite3.connect(path, timeout=10)
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('CREATE TABLE IF NOT EXISTS snapshot (id INTEGER PRIMARY KEY CHECK(id=1), body TEXT NOT NULL)')
    db.execute('CREATE TABLE IF NOT EXISTS sessions (hash TEXT PRIMARY KEY, player TEXT NOT NULL, authority INTEGER NOT NULL, expires INTEGER NOT NULL)')
    db.commit()
    return db


def token_hash(token):
    return hashlib.sha256(token.encode()).hexdigest()


class Host:
    def __init__(self, db, binary, config, ui=None, public_origin=None):
        self.db, self.binary, self.config = db, str(binary), Path(config)
        self.definition = json.loads(self.config.read_text())
        self.ui = Path(ui) if ui else ROOT/'examples/meta/ui'
        self.public_origin = public_origin
        if public_origin:
            url = urlsplit(public_origin)
            if url.scheme not in ('http', 'https') or not url.netloc or url.path or url.query or url.fragment or url.username or url.password:
                raise ValueError('public origin must be scheme://host[:port] without path or credentials')

    def call(self, token, request):
        if not isinstance(request, dict) or set(request) - {'service', 'action', 'operation', 'arguments'}:
            return 400, {'detail': 'invalid request'}
        if not isinstance(request.get('arguments', {}), dict):
            return 400, {'detail': 'arguments must be object'}
        for field in ('service', 'action', 'operation'):
            if not isinstance(request.get(field, ''), str):
                return 400, {'detail': 'request fields must be strings'}
        request = dict(request, arguments=request.get('arguments', {}))
        try:
            self.db.execute('BEGIN IMMEDIATE')
            user = self.db.execute('SELECT player,authority FROM sessions WHERE hash=? AND expires>?',
                                   (token_hash(token), int(time.time()))).fetchone()
            if not user:
                self.db.rollback()
                return 401, {'detail': 'session expired or unknown'}
            row = self.db.execute('SELECT body FROM snapshot WHERE id=1').fetchone()
            payload = dict(player=user[0], authority=bool(user[1]), now=int(time.time()),
                           state=json.loads(row[0]) if row else None, request=request)
            result = subprocess.run([self.binary, str(self.config)], input=json.dumps(payload)+'\n',
                                    text=True, capture_output=True, timeout=15, check=True)
            output = json.loads(result.stdout)
            code = output.get('code', 2)
            if code in (0, 1) and 'state' in output:
                self.db.execute('INSERT INTO snapshot(id,body) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET body=excluded.body',
                                (json.dumps(output['state'], separators=(',', ':')),))
                self.db.commit()
            else:
                self.db.rollback()
            # Complete server snapshot and receipts never cross the HTTP boundary.
            return 200, {k: v for k, v in output.items() if k != 'state'}
        except (ValueError, OSError, sqlite3.Error, subprocess.SubprocessError):
            self.db.rollback()
            return 503, {'detail': 'service unavailable; retry the same operation ID'}


def handler(host):
    class Handler(BaseHTTPRequestHandler):
        def send(self, status, body, mime='application/json; charset=utf-8'):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; style-src 'self'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            files = {'/': ('index.html', 'text/html; charset=utf-8'), '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
                     '/style.css': ('style.css', 'text/css; charset=utf-8'), '/lang.json': ('lang.json', 'application/json; charset=utf-8')}
            if self.path in files:
                name, mime = files[self.path]
                return self.send(200, (host.ui/name).read_bytes(), mime)
            if self.path == '/catalog':
                # Only definitions, never credentials or server state.
                public = copy.deepcopy(host.definition)
                for program in public.get("extensions", {}).get("liveops", {}).get("referrals", {}).values():
                    program.pop("codes", None)
                return self.send(200, public)
            self.send(404, {'detail': 'not found'})

        def do_POST(self):
            if self.path != '/rpc':
                return self.send(404, {'detail': 'not found'})
            # Same-origin browser client, no permissive CORS. Native clients can omit Origin.
            origin = self.headers.get('Origin')
            if origin and origin != (host.public_origin or 'http://' + self.headers.get('Host', '')):
                return self.send(403, {'detail': 'origin'})
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 65536:
                    return self.send(413, {'detail': 'request size'})
                if self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
                    return self.send(415, {'detail': 'expected JSON'})
                request = json.loads(self.rfile.read(size))
            except (ValueError, UnicodeError):
                return self.send(400, {'detail': 'invalid JSON'})
            auth = self.headers.get('Authorization', '')
            token = auth[7:] if auth.startswith('Bearer ') else ''
            status, body = host.call(token, request)
            self.send(status, body)

        def log_message(self, fmt, *args):
            pass  # Never log request bodies or bearer credentials.
    return Handler


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--db', type=Path, required=True)
    sub = p.add_subparsers(dest='command', required=True)
    provision = sub.add_parser('user', help='issue a bearer token for an authenticated player')
    provision.add_argument('player')
    provision.add_argument('--authority', action='store_true', help='trusted service/admin credential; never distribute to players')
    provision.add_argument('--days', type=int, default=7)
    revoke = sub.add_parser('revoke', help='revoke every session belonging to player')
    revoke.add_argument('player')
    serve = sub.add_parser('serve')
    serve.add_argument('--binary', type=Path, required=True)
    serve.add_argument('--config', type=Path, default=ROOT/'examples/meta/service.json')
    serve.add_argument('--public-origin', help='exact external origin, e.g. https://game.example, when behind TLS proxy')
    serve.add_argument('--ui', type=Path, help='project ui/meta directory')
    serve.add_argument('--port', type=int, default=8765)
    args = p.parse_args()
    db = database(args.db)
    if args.command == 'user':
        if not 0 < len(args.player.encode()) <= 128 or not 1 <= args.days <= 365:
            p.error('invalid player or session duration')
        token = secrets.token_urlsafe(32)
        db.execute('INSERT INTO sessions VALUES(?,?,?,?)', (token_hash(token), args.player, args.authority, int(time.time())+args.days*86400))
        db.commit()
        print(token)
    elif args.command == 'revoke':
        db.execute('DELETE FROM sessions WHERE player=?', (args.player,))
        db.commit()
    else:
        host = Host(db, args.binary.resolve(), args.config.resolve(), args.ui, args.public_origin)
        server = HTTPServer(('127.0.0.1', args.port), handler(host))
        print(f'Meta reference server: http://127.0.0.1:{server.server_port}', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
