"""Contacts mirror, number matching and T9 search. All methods run on the GTK main thread."""
import base64
import json
import re
import time

from gi.repository import GLib, GObject

T9 = {c: d for d, letters in {"2": "abc", "3": "def", "4": "ghi", "5": "jkl", "6": "mno",
                                "7": "pqrs", "8": "tuv", "9": "wxyz"}.items() for c in letters}
PHONE_TYPES = ["mobile", "home", "work", "main", "other"]
EMAIL_TYPES = ["home", "work", "other"]


def digits(s):
    return re.sub(r"\D", "", s or "")


def key(number):
    """Comparable form of a phone number: the last 10 digits (handles +91 / 0 prefixes)."""
    d = digits(number)
    return d[-10:] if len(d) >= 10 else d


def t9(text):
    return "".join(T9.get(ch, ch if ch.isdigit() else " ") for ch in (text or "").lower())


def pretty(number):
    """Light formatting for Indian mobiles, untouched otherwise."""
    d = digits(number)
    if number and number.startswith("+91") and len(d) == 12:
        return f"+91 {d[2:7]} {d[7:]}"
    if len(d) == 10 and not number.startswith("+"):
        return f"{d[:5]} {d[5:]}"
    return number or ""


class Contacts(GObject.Object):
    __gsignals__ = {"changed": (GObject.SignalFlags.RUN_FIRST, None, ())}

    def __init__(self, store, link):
        super().__init__()
        self.store = store
        self.link = link
        self._by_key = {}
        self._syncing = False
        self._reindex()
        link.connect("state-changed", lambda *_: self.link.online and self.sync())
        link.connect("event", self._on_event)

    # ---- sync ----

    def _on_event(self, _link, name, _data):
        if name == "contacts.changed":
            self.sync()

    def sync(self):
        if self._syncing or not self.link.online:
            return
        self._syncing = True
        self._flush_pending()
        since = self.store.get("contacts_since", 0)
        # Re-ask a little before the last sync so clock skew can't hide an edit.
        self.link.call("contacts.sync", {"since": max(0, since - 120_000)}, self._synced)

    def _synced(self, ok, r):
        self._syncing = False
        if not ok:
            return
        live = set(r["ids"])
        db = self.store.db
        known = {row[0] for row in db.execute("SELECT id FROM contacts")}
        gone = known - live
        db.executemany("DELETE FROM contacts WHERE id=?", [(i,) for i in gone])
        db.executemany("DELETE FROM contact_photos WHERE id=?", [(i,) for i in gone])
        for c in r["changed"]:
            self._upsert(c)
        db.commit()
        self.store.put("contacts_since", r["now"])
        self._reindex()
        self.emit("changed")
        self._fetch_missing_photos()

    def _upsert(self, c):
        words = " ".join([c["name"], c.get("org", "")] + [e["a"] for e in c["emails"]]).lower()
        self.store.db.execute(
            "INSERT OR REPLACE INTO contacts VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (c["id"], c["name"], c.get("given", ""), c.get("family", ""), c.get("org", ""), c.get("title", ""),
             int(c["starred"]), c["updated"], int(c["hasPhoto"]), c.get("lookup", ""),
             json.dumps(c["phones"]), json.dumps(c["emails"]), words, t9(c["name"]),
             " ".join(digits(p["n"]) for p in c["phones"])))
        if not c["hasPhoto"]:
            self.store.db.execute("DELETE FROM contact_photos WHERE id=?", (c["id"],))
        else:
            self.store.db.execute("UPDATE contact_photos SET updated=0 WHERE id=? AND updated<?", (c["id"], c["updated"]))

    def _fetch_missing_photos(self):
        rows = self.store.q("SELECT c.id FROM contacts c LEFT JOIN contact_photos p ON p.id=c.id "
                            "WHERE c.has_photo=1 AND (p.id IS NULL OR p.updated=0)")
        ids = [r[0] for r in rows]

        def next_one(*_):
            if not ids or not self.link.online:
                return
            cid = ids.pop()
            self.link.call("contacts.photo", {"id": cid}, lambda ok, r: (store_photo(ok, r), next_one()))

        def store_photo(ok, r):
            if ok and r.get("jpeg"):
                self.store.x("INSERT OR REPLACE INTO contact_photos VALUES(?,?,?)",
                             (r["id"], base64.b64decode(r["jpeg"]), int(time.time() * 1000)))
                if not ids or len(ids) % 20 == 0:
                    self.emit("changed")

        # A few requests in flight at once keeps the first sync quick without flooding the link.
        for _ in range(4):
            next_one()

    def _reindex(self):
        self._by_key = {}
        for row in self.store.q("SELECT id, name, phones FROM contacts"):
            for p in json.loads(row["phones"]):
                self._by_key.setdefault(key(p["n"]), (row["id"], row["name"]))

    # ---- queries ----

    def all(self):
        return self.store.q("SELECT * FROM contacts ORDER BY starred DESC, name COLLATE NOCASE")

    def get(self, cid):
        return self.store.q("SELECT * FROM contacts WHERE id=?", (cid,))[0] if cid else None

    def photo(self, cid):
        r = self.store.q("SELECT jpeg FROM contact_photos WHERE id=?", (cid,))
        return r[0][0] if r else None

    def lookup(self, number):
        """(contact_id, name) for a number, or (None, None)."""
        return self._by_key.get(key(number), (None, None))

    def search(self, text, limit=50):
        text = (text or "").strip().lower()
        if not text:
            return self.all()[:limit]
        like = f"%{text}%"
        d = digits(text)
        return self.store.q(
            "SELECT * FROM contacts WHERE search LIKE ? OR (? != '' AND digits LIKE ?) "
            "ORDER BY starred DESC, name COLLATE NOCASE LIMIT ?",
            (like, d, f"%{d}%", limit))

    def t9_search(self, query, limit=30):
        """Contacts whose name spells the keypad digits (word prefix or anywhere) or whose number contains them."""
        q = digits(query)
        if not q:
            return []
        scored = []
        for row in self.store.q("SELECT * FROM contacts"):
            name_t9 = row["t9"]
            words = name_t9.split()
            if any(w.startswith(q) for w in words):
                score = 0
            elif q in name_t9.replace(" ", ""):
                score = 1
            elif q in row["digits"]:
                score = 2
            else:
                continue
            scored.append((score, -row["starred"], row["name"].lower(), row))
        scored.sort(key=lambda s: s[:3])
        return [s[3] for s in scored[:limit]]

    # ---- edits (queued while offline) ----

    def save(self, data, cb=None):
        self._edit("contacts.save", data, cb)

    def delete(self, cid, cb=None):
        self.store.x("DELETE FROM contacts WHERE id=?", (cid,))
        self._reindex()
        self.emit("changed")
        self._edit("contacts.delete", {"id": cid}, cb)

    def star(self, cid, on, cb=None):
        self.store.x("UPDATE contacts SET starred=? WHERE id=?", (int(on), cid))
        self.emit("changed")
        self._edit("contacts.star", {"id": cid, "on": on}, cb)

    def set_photo(self, cid, jpeg_bytes, cb=None):
        self.store.x("INSERT OR REPLACE INTO contact_photos VALUES(?,?,?)", (cid, jpeg_bytes, int(time.time() * 1000)))
        self.emit("changed")
        self._edit("contacts.setPhoto", {"id": cid, "jpeg": base64.b64encode(jpeg_bytes).decode()}, cb)

    def _edit(self, op, payload, cb):
        if not self.link.online:
            self.store.x("INSERT INTO pending(op, payload, ts) VALUES(?,?,?)", (op, json.dumps(payload), int(time.time() * 1000)))
            if cb:
                GLib.idle_add(cb, True, None)
            return

        def done(ok, r):
            if ok and op == "contacts.save" and r:
                self._upsert(r)
                self.store.db.commit()
                self._reindex()
                self.emit("changed")
            if cb:
                cb(ok, r)

        self.link.call(op, payload, done)

    def _flush_pending(self):
        for row in self.store.q("SELECT * FROM pending ORDER BY id"):
            self.store.x("DELETE FROM pending WHERE id=?", (row["id"],))
            self.link.call(row["op"], json.loads(row["payload"]))

    def pending_count(self):
        return self.store.q("SELECT COUNT(*) FROM pending")[0][0]

    # ---- vCard ----

    def export_vcf(self, path):
        out = []
        for c in self.all():
            out += ["BEGIN:VCARD", "VERSION:3.0", f"FN:{_esc(c['name'])}",
                    f"N:{_esc(c['family'])};{_esc(c['given'])};;;"]
            if c["org"] or c["title"]:
                out.append(f"ORG:{_esc(c['org'])}")
                if c["title"]:
                    out.append(f"TITLE:{_esc(c['title'])}")
            for p in json.loads(c["phones"]):
                out.append(f"TEL;TYPE={_vtype(p['type'], 'CELL')}:{p['n']}")
            for e in json.loads(c["emails"]):
                out.append(f"EMAIL;TYPE={_vtype(e['type'], 'INTERNET')}:{e['a']}")
            photo = self.photo(c["id"])
            if photo:
                out.append("PHOTO;ENCODING=b;TYPE=JPEG:" + base64.b64encode(photo).decode())
            out.append("END:VCARD")
        with open(path, "w") as f:
            f.write("\r\n".join(out) + "\r\n")
        return len(self.all())

    def import_vcf(self, path):
        """Adds every card as a new phone contact. Returns how many were queued."""
        with open(path, encoding="utf-8", errors="replace") as f:
            text = re.sub(r"\r?\n[ \t]", "", f.read())
        count = 0
        for card in re.findall(r"BEGIN:VCARD(.*?)END:VCARD", text, re.S | re.I):
            data = {"name": "", "given": "", "family": "", "org": "", "title": "", "phones": [], "emails": []}
            for line in card.splitlines():
                if ":" not in line:
                    continue
                head, val = line.split(":", 1)
                prop = head.split(";")[0].upper()
                params = head.upper()
                if prop == "FN":
                    data["name"] = _unesc(val)
                elif prop == "N":
                    parts = (val.split(";") + ["", ""])[:2]
                    data["family"], data["given"] = _unesc(parts[0]), _unesc(parts[1])
                elif prop == "ORG":
                    data["org"] = _unesc(val.split(";")[0])
                elif prop == "TITLE":
                    data["title"] = _unesc(val)
                elif prop == "TEL":
                    t = "work" if "WORK" in params else "home" if "HOME" in params else "mobile"
                    data["phones"].append({"n": val.strip(), "type": t})
                elif prop == "EMAIL":
                    t = "work" if "WORK" in params else "home"
                    data["emails"].append({"a": val.strip(), "type": t})
            if data["name"] or data["given"] or data["family"]:
                self.save(data)
                count += 1
        return count


def _esc(s):
    return (s or "").replace("\\", "\\\\").replace(",", "\\,").replace(";", "\\;").replace("\n", "\\n")


def _unesc(s):
    return s.replace("\\n", "\n").replace("\\,", ",").replace("\\;", ";").replace("\\\\", "\\").strip()


def _vtype(t, default):
    return {"mobile": "CELL", "home": "HOME", "work": "WORK", "main": "MAIN"}.get(t, default)
