-- Server-only SQLite reference storage. Never copy sessions/snapshots into client bundles.
CREATE TABLE IF NOT EXISTS snapshot (id INTEGER PRIMARY KEY CHECK(id=1), body TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS sessions (hash TEXT PRIMARY KEY, player TEXT NOT NULL, authority INTEGER NOT NULL, expires INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS sessions_player ON sessions(player);
CREATE INDEX IF NOT EXISTS sessions_expiry ON sessions(expires);
