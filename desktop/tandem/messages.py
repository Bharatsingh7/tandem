"""Conversations: SMS threads read live from the phone, plus chats (WhatsApp, Telegram…) rebuilt
from messaging notifications and kept in SQLite so history outlives the notification."""
import base64
import time

from gi.repository import GLib, GObject


def chat_key(n):
    return f"{n['pkg']}|{n.get('shortcut') or n['conversation']['title']}"


class MessageCenter(GObject.Object):
    __gsignals__ = {
        "changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
        "chat-updated": (GObject.SignalFlags.RUN_FIRST, None, (str,)),
        "sms-updated": (GObject.SignalFlags.RUN_FIRST, None, ()),
    }

    def __init__(self, app):
        super().__init__()
        self.app = app
        self.link = app.link
        self.store = app.store
        self.threads = []
        self.sms_pkg = None
        self.link.connect("event", self._on_event)
        self.link.connect("state-changed", self._on_state)

    def _on_state(self, link):
        if link.online:
            # Chats whose notification vanished while we were away can't be replied to any more.
            self.store.x("UPDATE chats SET notif_key=NULL")
            self.refresh_sms()

    def _on_event(self, _l, name, d):
        if name == "notif.posted" and d.get("conversation") and d["pkg"] != self.sms_pkg:
            self.ingest(d)
        elif name == "notif.removed":
            rows = self.store.q("SELECT key FROM chats WHERE notif_key=?", (d["key"],))
            if rows:
                # Removed on the phone usually means it was read there.
                self.store.x("UPDATE chats SET notif_key=NULL, unread=0 WHERE notif_key=?", (d["key"],))
                self.emit("changed")
        elif name in ("sms.changed", "sms.received"):
            GLib.timeout_add(600 if name == "sms.received" else 0, lambda: self.refresh_sms() and False)
        elif name == "sms.sent" and not d.get("ok"):
            self.app.toast("SMS failed to send")
        elif name == "status":
            self.sms_pkg = d.get("smsApp") or self.sms_pkg

    # ---- chats from notifications ----

    def ingest(self, n):
        conv = n["conversation"]
        key = chat_key(n)
        db = self.store.db
        new = 0
        for m in conv["messages"]:
            if not m["text"] and not m.get("media"):
                continue
            text = m["text"] or f"[{m['media'].split('/')[0]}]"
            if m["self"] and db.execute(
                    "SELECT 1 FROM chat_messages WHERE chat_key=? AND self=1 AND text=? AND ABS(time-?)<180000",
                    (key, text, m["time"])).fetchone():
                continue
            cur = db.execute("INSERT OR IGNORE INTO chat_messages VALUES(?,?,?,?,?)",
                             (key, m["time"], m["sender"], text, int(m["self"])))
            new += cur.rowcount if not m["self"] else 0
        last = conv["messages"][-1] if conv["messages"] else {"text": n.get("text", ""), "time": n["time"]}
        avatar = base64.b64decode(n["avatar"]) if n.get("avatar") and not conv.get("group") else None
        db.execute(
            "INSERT INTO chats(key, pkg, app, title, is_group, last_time, last_text, unread, notif_key, avatar) "
            "VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(key) DO UPDATE SET title=excluded.title, is_group=excluded.is_group, "
            "last_time=MAX(last_time, excluded.last_time), last_text=excluded.last_text, unread=unread+?, "
            "notif_key=excluded.notif_key, avatar=COALESCE(excluded.avatar, avatar), app=excluded.app",
            (key, n["pkg"], n["app"], conv["title"], int(conv.get("group", False)), last["time"],
             last.get("text") or "", new, n["key"], avatar, new))
        db.commit()
        self.emit("chat-updated", key)
        self.emit("changed")

    def chats(self):
        return self.store.q("SELECT * FROM chats ORDER BY last_time DESC")

    def chat(self, key):
        r = self.store.q("SELECT * FROM chats WHERE key=?", (key,))
        return r[0] if r else None

    def chat_messages(self, key, limit=400):
        return list(reversed(self.store.q(
            "SELECT * FROM chat_messages WHERE chat_key=? ORDER BY time DESC LIMIT ?", (key, limit))))

    def mark_read(self, key):
        self.store.x("UPDATE chats SET unread=0 WHERE key=?", (key,))
        self.emit("changed")

    def delete_chat(self, key):
        self.store.x("DELETE FROM chat_messages WHERE chat_key=?", (key,))
        self.store.x("DELETE FROM chats WHERE key=?", (key,))
        self.emit("changed")

    def reply(self, key, text, cb):
        chat = self.chat(key)
        if not chat or not chat["notif_key"]:
            cb(False, "This chat has no live notification on the phone, so it can't be replied to from here.")
            return

        def done(ok, r):
            if ok:
                self.store.x("INSERT OR IGNORE INTO chat_messages VALUES(?,?,?,?,1)", (key, int(time.time() * 1000), "You", text))
                self.store.x("UPDATE chats SET last_time=?, last_text=?, unread=0 WHERE key=?", (int(time.time() * 1000), text, key))
                self.emit("chat-updated", key)
                self.emit("changed")
            cb(ok, r)

        self.app.notifs.reply(chat["notif_key"], text, done)

    # ---- SMS ----

    def refresh_sms(self):
        def got(ok, threads):
            if ok:
                self.threads = threads
                self.emit("sms-updated")
                self.emit("changed")
        self.link.call("sms.threads", {"limit": 80}, got)
        return True

    def sms_messages(self, thread, cb):
        self.link.call("sms.messages", {"thread": thread, "limit": 300}, cb)

    def send_sms(self, address, body, cb=None):
        self.link.call("sms.send", {"address": address, "body": body}, cb)

    def thread_for(self, number):
        from .contacts import key
        k = key(number)
        return next((t for t in self.threads if key(t["address"]) == k), None)
