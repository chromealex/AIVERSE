"""Verify legacy upgrade, repeat initialization and fail-closed schema handling."""
import importlib.util
from pathlib import Path
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('meta_server', ROOT/'tools/meta/server.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)/'legacy.sqlite'
    db = sqlite3.connect(path)
    db.executescript("CREATE TABLE snapshot (id INTEGER PRIMARY KEY, body TEXT NOT NULL);"
                     "CREATE TABLE sessions (hash TEXT PRIMARY KEY, player TEXT NOT NULL, authority INTEGER NOT NULL, expires INTEGER NOT NULL);")
    db.execute('INSERT INTO snapshot VALUES(1,?)', ('{"revision":7}',))
    db.execute('INSERT INTO sessions VALUES(?,?,?,?)', ('hashed-token','alice',0,9999))
    db.commit()
    db.close()
    for _ in range(2):
        db = server.database(path)
        assert db.execute('SELECT body FROM snapshot').fetchone()[0] == '{"revision":7}'
        assert db.execute('SELECT player FROM sessions').fetchone()[0] == 'alice'
        assert db.execute('SELECT version FROM meta_schema').fetchall() == [(1,)]
        db.close()
    db = sqlite3.connect(path)
    db.execute('INSERT INTO meta_schema VALUES(2)')
    db.commit()
    before = list(db.iterdump())
    db.close()
    try:
        server.database(path)
    except ValueError:
        pass
    else:
        raise AssertionError('newer schema was accepted')
    db = sqlite3.connect(path)
    assert list(db.iterdump()) == before
    db.close()
    db = server.database(Path(directory)/'fresh.sqlite')
    assert db.execute('SELECT version FROM meta_schema').fetchall() == [(1,)]
    assert db.execute('SELECT COUNT(*) FROM snapshot').fetchone()[0] == 0
    db.close()
print('Meta database migrations passed')
