import base64

from gi.repository import Adw, GLib, Gtk

from ..config import cfg, save
from ..widgets import clear, label, scrolled, when
from .base import Page


class NotificationsPage(Page):
    title = "Notifications"

    def __init__(self, app, win):
        super().__init__(app, win)
        clear_all = Gtk.Button(label="Clear all", css_classes=["flat"])
        clear_all.connect("clicked", lambda *_: app.notifs.dismiss())
        self.header.pack_end(clear_all)
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_top=18, margin_bottom=18,
                           margin_start=18, margin_end=18)
        self.stack = Gtk.Stack()
        self.stack.add_named(Adw.StatusPage(icon_name="preferences-system-notifications-symbolic", title="All caught up",
                                            description="Phone notifications appear here and as desktop popups."), "empty")
        self.stack.add_named(scrolled(Adw.Clamp(maximum_size=720, child=self.box)), "list")
        self.set_body(self.stack)
        app.notifs.connect("changed", lambda *_: self._fill())
        self._fill()

    def _fill(self):
        clear(self.box)
        items = [n for n in self.app.notifs.visible() if not n.get("media")]
        self.stack.set_visible_child_name("list" if items else "empty")
        for n in items:
            self.box.append(self._card(n))

    def _card(self, n):
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, css_classes=["card-soft"])
        top = Gtk.Box(spacing=8)
        icon = self.app.notifs.icons.get(n["pkg"])
        if icon:
            from ..widgets import texture
            img = Gtk.Image(pixel_size=18)
            img.set_from_paintable(texture(icon))
            top.append(img)
        app_l = label(n["app"], css=("notif-app",))
        app_l.set_hexpand(True)
        top.append(app_l)
        top.append(label(when(n["time"]), css=("caption", "muted")))
        mute = Gtk.MenuButton(icon_name="view-more-symbolic", css_classes=["flat", "circular"])
        pop = Gtk.Popover()
        pb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        muted = n["pkg"] in cfg.get("muted_apps", [])
        mb = Gtk.Button(label=("Show popups from " if muted else "Mute popups from ") + n["app"], css_classes=["flat"])
        mb.connect("clicked", lambda *_: (pop.popdown(), self._mute(n["pkg"])))
        pb.append(mb)
        ob = Gtk.Button(label="Open on phone", css_classes=["flat"])
        ob.connect("clicked", lambda *_: (pop.popdown(), self.app.link.call("notif.open", {"key": n["key"]})))
        pb.append(ob)
        pop.set_child(pb)
        mute.set_popover(pop)
        top.append(mute)
        if n.get("clearable"):
            x = Gtk.Button(icon_name="window-close-symbolic", css_classes=["flat", "circular"], tooltip_text="Dismiss on phone")
            x.connect("clicked", lambda *_: self.app.notifs.dismiss(n["key"]))
            top.append(x)
        card.append(top)

        body = Gtk.Box(spacing=12)
        text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
        if n.get("title"):
            text.append(label(n["title"], css=("heading",), wrap=True))
        conv = n.get("conversation")
        if conv and conv.get("messages"):
            for m in conv["messages"][-4:]:
                who = "You" if m["self"] else m["sender"]
                text.append(label(f"{who}: {m['text']}" if conv.get("group") or m["self"] else m["text"], wrap=True))
        elif n.get("big") or n.get("text"):
            text.append(label(n.get("big") or n["text"], wrap=True, lines=6, ellipsize=True))
        if n.get("progress"):
            p = n["progress"]
            pr = Gtk.ProgressBar(fraction=p["v"] / max(1, p["max"]))
            text.append(pr)
        body.append(text)
        if n.get("avatar"):
            from ..widgets import avatar
            body.append(avatar(n.get("title"), 44, base64.b64decode(n["avatar"])))
        card.append(body)

        acts = [a for a in n.get("actions", []) if not a["reply"]]
        can_reply = any(a["reply"] for a in n.get("actions", []))
        if acts or can_reply:
            row = Gtk.Box(spacing=6)
            for a in acts[:3]:
                b = Gtk.Button(label=a["title"], css_classes=["pill", "small"])
                b.connect("clicked", lambda _b, i=a["i"]: self.app.notifs.action(n["key"], i))
                row.append(b)
            card.append(row)
        if can_reply:
            entry = Gtk.Entry(placeholder_text="Reply…", hexpand=True)
            def send(e):
                t = e.get_text().strip()
                if t:
                    e.set_sensitive(False)
                    self.app.notifs.reply(n["key"], t, lambda ok, r: (e.set_sensitive(True), e.set_text("") if ok else
                                                                      self.app.toast(str(r))))
            entry.connect("activate", send)
            card.append(entry)
        return card

    def _mute(self, pkg):
        m = set(cfg.get("muted_apps", []))
        m.symmetric_difference_update({pkg})
        cfg["muted_apps"] = sorted(m)
        save(cfg)
        self._fill()
