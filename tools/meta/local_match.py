#!/usr/bin/env python3
"""Issue local match-bound credentials from existing authenticated meta sessions.
Local trusted-files bridge, not an Internet identity provider. Never prints tokens.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import tempfile
import time


def private_write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.match-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as output:
            output.write(text)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def database(path):
    if not path.is_file():
        raise ValueError('existing meta database required')
    os.chmod(path, 0o600)
    for suffix in ('-wal', '-shm'):
        sidecar = Path(str(path)+suffix)
        if sidecar.exists():
            os.chmod(sidecar, 0o600)
    db = sqlite3.connect(path, timeout=10)
    db.execute('CREATE TABLE IF NOT EXISTS local_matches (audience TEXT PRIMARY KEY, slots INTEGER NOT NULL)')
    db.execute('CREATE TABLE IF NOT EXISTS local_match_players (audience TEXT, player TEXT, slot INTEGER, PRIMARY KEY(audience,player), UNIQUE(audience,slot))')
    db.execute('CREATE TABLE IF NOT EXISTS local_match_tickets (audience TEXT, player TEXT, token TEXT UNIQUE, expires INTEGER, PRIMARY KEY(audience,player))')
    db.commit()
    return db


def issue(db, player_token, audience, slots, ttl, now):
    if not audience or len(audience.encode()) > 128 or not 1 <= slots <= 200 or not 1 <= ttl <= 900:
        raise ValueError('invalid audience, slots or TTL')
    try:
        db.execute('BEGIN IMMEDIATE')
        principal = db.execute('SELECT player,authority,expires FROM sessions WHERE hash=? AND expires>?',
                              (hashlib.sha256(player_token.encode()).hexdigest(), now)).fetchone()
        if not principal or principal[1]:
            raise ValueError('ordinary authenticated player session required')
        player = principal[0]
        capacity = db.execute('SELECT slots FROM local_matches WHERE audience=?', (audience,)).fetchone()
        if capacity and capacity[0] != slots:
            raise ValueError('match capacity cannot change; use a new match audience')
        db.execute('INSERT OR IGNORE INTO local_matches VALUES(?,?)', (audience, slots))
        allocation = db.execute('SELECT slot FROM local_match_players WHERE audience=? AND player=?', (audience, player)).fetchone()
        if allocation:
            slot = allocation[0]
        else:
            used = {row[0] for row in db.execute('SELECT slot FROM local_match_players WHERE audience=?', (audience,))}
            slot = next((slot for slot in range(1, slots+1) if slot not in used), None)
            if slot is None:
                raise ValueError('match is full; expired players retain their identity-bound slot')
            db.execute('INSERT INTO local_match_players VALUES(?,?,?)', (audience, player, slot))
        expires = min(now+ttl, principal[2])
        token = secrets.token_urlsafe(32)
        db.execute('INSERT INTO local_match_tickets VALUES(?,?,?,?) ON CONFLICT(audience,player) DO UPDATE SET token=excluded.token,expires=excluded.expires', (audience, player, token, expires))
        db.commit()
        return dict(player=player, audience=audience, slot=slot, token=token, expires=expires)
    except Exception:
        db.rollback()
        raise


def export(db, path, now):
    # Lock after issuance committed, then read the latest committed records. Atomic
    # replace while locked prevents concurrent exporters from writing an older view.
    try:
        db.execute('BEGIN IMMEDIATE')
        rows = db.execute('SELECT t.player,t.audience,p.slot,t.token,t.expires FROM local_match_tickets t JOIN local_match_players p USING(audience,player) WHERE t.expires>? ORDER BY t.audience,p.slot', (now,)).fetchall()
        if len(rows) > 10000:
            raise ValueError('too many active local tickets')
        tickets = [dict(player=p,audience=a,slot=s,token=t,expires=e) for p,a,s,t,e in rows]
        private_write(path, json.dumps(dict(version=1,tickets=tickets), separators=(',', ':'))+'\n')
        db.commit()
    except Exception:
        db.rollback()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    sub = parser.add_subparsers(dest='command', required=True)
    add = sub.add_parser('issue')
    add.add_argument('--player-token-file', type=Path, required=True)
    add.add_argument('--client-token-file', type=Path, required=True)
    add.add_argument('--audience', required=True)
    add.add_argument('--slots', type=int, required=True)
    add.add_argument('--ttl', type=int, default=300)
    remove = sub.add_parser('revoke')
    remove.add_argument('--audience', required=True)
    remove.add_argument('--player', required=True)
    sub.add_parser('export')
    args = parser.parse_args()
    db = database(args.db)
    try:
        now = int(time.time())
        if args.command == 'issue':
            record = issue(db, args.player_token_file.read_text().strip(), args.audience, args.slots, args.ttl, now)
            private_write(args.client_token_file, record.pop('token')+'\n')
            export(db, args.out, now)
            print(json.dumps(record))
        elif args.command == 'revoke':
            db.execute('DELETE FROM local_match_tickets WHERE audience=? AND player=?', (args.audience,args.player))
            db.commit()
            export(db, args.out, now)
            print('Match credential revoked; player slot retained')
        else:
            export(db, args.out, now)
            print('Local admission table exported')
    finally:
        db.close()

if __name__ == '__main__':
    main()
