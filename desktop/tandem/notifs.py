"""Mirrored phone notifications: kept for the Notifications page and echoed as desktop popups."""
import base64

from gi.repository import GObject

from .config import cfg

# Handled by dedicated features, or pure noise on a desktop.
SKIP_POPUP_CATEGORIES = {"call", "transport", "progress", "service", "sys", "navigation", "stopwatch"}
MESSAGING_HINT = {"msg", "email", "social"}


class NotificationCenter(GObject.Object):
    __gsignals__ = {
        "changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
        "posted": (GObject.SignalFlags.RUN_FIRST, None, (object,)),
    }

    def __init__(self, app):
        super().__init__()
        self.app = app
        self.link = app.link
        self.items = {}       # key -> notification dict
        self.icons = {}       # pkg -> png bytes
        self._popups = {}     # key -> desktop notification id
        self._wanted_icons = set()
        self.link.connect("event", self._on_event)
        self.link.connect("state-changed", self._on_state)

    def _on_state(self, link):
        if link.online:
            def got(ok, items):
                if ok:
                    self.items = {n["key"]: n for n in items}
                    for n in items:
                        self._icon(n["pkg"])
                    self.emit("changed")
            link.call("notif.list", cb=got)

    def _icon(self, pkg):
        if pkg in self.icons or pkg in self._wanted_icons:
            return
        self._wanted_icons.add(pkg)

        def got(ok, r):
            if ok:
                self.icons[pkg] = base64.b64decode(r["png"])
                self.emit("changed")
        self.link.call("notif.icon", {"pkg": pkg}, got)

    def _on_event(self, _l, name, data):
        if name == "notif.posted":
            old = self.items.get(data["key"])
            self.items[data["key"]] = data
            self._icon(data["pkg"])
            self.emit("posted", data)
            self.emit("changed")
            if self._is_new_content(old, data):
                self._popup(data)
        elif name == "notif.removed":
            self.items.pop(data["key"], None)
            nid = self._popups.pop(data["key"], None)
            if nid:
                self.app.notifier.close(nid)
            self.emit("changed")

    @staticmethod
    def _is_new_content(old, new):
        if old is None:
            return True
        return (old.get("title"), old.get("text"), len((old.get("conversation") or {}).get("messages", []))) != \
               (new.get("title"), new.get("text"), len((new.get("conversation") or {}).get("messages", [])))

    def _popup(self, n):
        if not cfg.get("notify_mirror") or n.get("ongoing") or n.get("media") or n.get("progress"):
            return
        if n["pkg"] in cfg.get("muted_apps", []) or n.get("category") in SKIP_POPUP_CATEGORIES:
            return
        if self.app.window_focused_on("messages") and n.get("conversation"):
            return
        title = n.get("title") or n["app"]
        body = n.get("text") or n.get("big") or ""
        conv = n.get("conversation")
        if conv and conv.get("messages"):
            last = conv["messages"][-1]
            title = conv["title"] if not conv.get("group") else f"{conv['title']}"
            body = f"{last['sender']}: {last['text']}" if conv.get("group") else last["text"]
        actions = [("open", "Open on phone")]
        if any(a["reply"] for a in n.get("actions", [])):
            actions.insert(0, ("reply", "Reply"))
        for a in n.get("actions", []):
            if not a["reply"] and len(actions) < 4:
                actions.append((f"a{a['i']}", a["title"]))
        image = None
        if n.get("avatar"):
            image = self.app.notifier.image_file(base64.b64decode(n["avatar"]))
        elif n["pkg"] in self.icons:
            image = self.app.notifier.image_file(self.icons[n["pkg"]])

        def act(key):
            if key == "reply":
                self.app.present("messages", chat=n)
            elif key == "open":
                self.link.call("notif.open", {"key": n["key"]})
            elif key == "default":
                self.app.present("messages" if conv else "notifications", chat=n if conv else None)
            elif key.startswith("a"):
                self.link.call("notif.action", {"key": n["key"], "index": int(key[1:])})

        actions.insert(0, ("default", "Open"))
        self._popups[n["key"]] = self.app.notifier.notify(
            f"{title}", body[:300], actions, act, image=image, replaces=self._popups.get(n["key"], 0),
            category="im.received" if conv else None)

    # ---- actions ----

    def dismiss(self, key=None):
        self.link.call("notif.dismiss", {"key": key} if key else {})
        if key:
            self.items.pop(key, None)
        else:
            self.items = {k: v for k, v in self.items.items() if not v.get("clearable")}
        self.emit("changed")

    def reply(self, key, text, cb=None):
        self.link.call("notif.reply", {"key": key, "text": text}, cb)

    def action(self, key, index):
        self.link.call("notif.action", {"key": key, "index": index})

    def visible(self):
        return sorted(self.items.values(), key=lambda n: -n["time"])
