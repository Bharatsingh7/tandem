"""Local SQLite mirror: contacts (offline search), call log, chat history, pending edits."""
import json
import sqlite3

from .config import DATA_DIR

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT);
CREATE TABLE IF NOT EXISTS contacts(
    id INTEGER PRIMARY KEY, name TEXT, given TEXT, family TEXT, org TEXT, title TEXT,
    starred INTEGER, updated INTEGER, has_photo INTEGER, lookup TEXT,
    phones TEXT, emails TEXT, search TEXT, t9 TEXT, digits TEXT);
CREATE TABLE IF NOT EXISTS contact_photos(id INTEGER PRIMARY KEY, jpeg BLOB, updated INTEGER);
CREATE TABLE IF NOT EXISTS pending(id INTEGER PRIMARY KEY AUTOINCREMENT, op TEXT, payload TEXT, ts INTEGER);
CREATE TABLE IF NOT EXISTS calllog(
    id INTEGER PRIMARY KEY, number TEXT, name TEXT, type TEXT, date INTEGER, duration INTEGER, account TEXT, place TEXT);
CREATE TABLE IF NOT EXISTS chats(
    key TEXT PRIMARY KEY, pkg TEXT, app TEXT, title TEXT, is_group INTEGER, last_time INTEGER,
    last_text TEXT, unread INTEGER DEFAULT 0, notif_key TEXT, avatar BLOB);
CREATE TABLE IF NOT EXISTS chat_messages(
    chat_key TEXT, time INTEGER, sender TEXT, text TEXT, self INTEGER,
    UNIQUE(chat_key, time, sender, text));
CREATE INDEX IF NOT EXISTS chat_messages_by_chat ON chat_messages(chat_key, time);
CREATE INDEX IF NOT EXISTS calllog_by_date ON calllog(date DESC);
"""


class Store:
    def __init__(self, path=None):
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path or DATA_DIR / "tandem.db")
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)

    def get(self, k, default=None):
        r = self.db.execute("SELECT v FROM meta WHERE k=?", (k,)).fetchone()
        return json.loads(r[0]) if r else default

    def put(self, k, v):
        self.db.execute("INSERT OR REPLACE INTO meta VALUES(?,?)", (k, json.dumps(v)))
        self.db.commit()

    def q(self, sql, args=()):
        return self.db.execute(sql, args).fetchall()

    def x(self, sql, args=()):
        cur = self.db.execute(sql, args)
        self.db.commit()
        return cur
