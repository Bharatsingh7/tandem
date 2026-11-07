import json

from gi.repository import Adw, Gdk, GLib, Gtk

from ..contacts import digits, pretty
from ..widgets import avatar, clear, day_label, duration, label, scrolled
from .base import Page

KEYS = [("1", ""), ("2", "ABC"), ("3", "DEF"), ("4", "GHI"), ("5", "JKL"), ("6", "MNO"),
        ("7", "PQRS"), ("8", "TUV"), ("9", "WXYZ"), ("*", ""), ("0", "+"), ("#", "")]
TYPE_ICON = {"in": "call-incoming-symbolic", "out": "call-outgoing-symbolic", "missed": "call-missed-symbolic",
             "rejected": "call-missed-symbolic", "blocked": "call-stop-symbolic", "voicemail": "call-incoming-symbolic"}
TYPE_TEXT = {"in": "Incoming", "out": "Outgoing", "missed": "Missed", "rejected": "Declined", "blocked": "Blocked",
             "voicemail": "Voicemail", "other": "Call"}


class CallsPage(Page):
    title = "Calls"

    def __init__(self, app, win):
        super().__init__(app, win)
        body = Gtk.Box(spacing=0)

        # ---- dialer column ----
        dial = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_top=18, margin_bottom=18,
                       margin_start=22, margin_end=22, width_request=330)
        dial.append(label("DIALER", css=("eyebrow",)))
        self.entry = Gtk.Entry(css_classes=["dial-entry", "flat"], xalign=0.5, placeholder_text="Type a name or number")
        self.entry.connect("changed", lambda *_: self._suggest())
        self.entry.connect("activate", lambda *_: self._call_entry())
        dial.append(self.entry)
        self.match_label = label("", css=("caption", "muted"), xalign=0.5, ellipsize=True)
        dial.append(self.match_label)
        self.suggest = Gtk.ListBox(css_classes=["boxed-list"], selection_mode=Gtk.SelectionMode.NONE)
        self.suggest.connect("row-activated", lambda _l, row: self._pick(row))
        sw = Gtk.ScrolledWindow(min_content_height=0, max_content_height=190, propagate_natural_height=True)
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.set_child(self.suggest)
        dial.append(sw)

        grid = Gtk.Grid(row_spacing=10, column_spacing=14, halign=Gtk.Align.CENTER, css_classes=["dialpad"], margin_top=6)
        for i, (d, letters) in enumerate(KEYS):
            b = Gtk.Button(css_classes=["flat"])
            v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER)
            v.append(label(d, css=("digit",), xalign=0.5))
            v.append(label(letters or " ", css=("letters",), xalign=0.5))
            b.set_child(v)
            b.connect("clicked", lambda _b, ch=d: self._press(ch))
            if d == "0":
                lp = Gtk.GestureLongPress()
                lp.connect("pressed", lambda *_: self._press("+", replace_last="0"))
                b.add_controller(lp)
            grid.attach(b, i % 3, i // 3, 1, 1)
        dial.append(grid)

        actions = Gtk.Box(spacing=18, halign=Gtk.Align.CENTER, margin_top=6)
        self.sim = Gtk.DropDown(valign=Gtk.Align.CENTER, visible=False)
        actions.append(self.sim)
        call = Gtk.Button(icon_name="call-start-symbolic", css_classes=["call-btn", "call-green"], tooltip_text="Call")
        call.connect("clicked", lambda *_: self._call_entry())
        actions.append(call)
        back = Gtk.Button(icon_name="edit-clear-symbolic", css_classes=["flat", "circular"], valign=Gtk.Align.CENTER,
                          tooltip_text="Delete")
        back.connect("clicked", lambda *_: self._backspace())
        lp = Gtk.GestureLongPress()
        lp.connect("pressed", lambda *_: self.entry.set_text(""))
        back.add_controller(lp)
        actions.append(back)
        dial.append(actions)
        body.append(dial)
        body.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))

        # ---- recents / favourites ----
        right = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, hexpand=True)
        self.tabs = Adw.ViewStack()
        self.recents = Gtk.ListBox(css_classes=["navigation-sidebar"])
        self.recents.connect("row-activated", lambda _l, row: self.app.calls.dial(row.number))
        self.tabs.add_titled_with_icon(scrolled(self.recents), "recents", "Recents", "document-open-recent-symbolic")
        self.favs = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=6, min_children_per_line=2,
                                margin_top=18, margin_start=18, margin_end=18, row_spacing=18, column_spacing=12,
                                valign=Gtk.Align.START, homogeneous=True)
        self.tabs.add_titled_with_icon(scrolled(self.favs), "favourites", "Favourites", "starred-symbolic")
        sw = Adw.ViewSwitcher(stack=self.tabs, policy=Adw.ViewSwitcherPolicy.WIDE)
        self.header.set_title_widget(sw)
        self.filter = Adw.ToggleGroup(margin_top=10, margin_start=14, margin_end=14, halign=Gtk.Align.START)
        for name, title in (("all", "All"), ("missed", "Missed"), ("in", "Incoming"), ("out", "Outgoing")):
            self.filter.add(Adw.Toggle(name=name, label=title))
        self.filter.set_active_name("all")
        self.filter.connect("notify::active-name", lambda *_: self._fill_recents())
        right.append(self.filter)
        right.append(self.tabs)
        body.append(right)
        self.set_body(body)

        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", self._key)
        self.add_controller(keys)

        app.calls.connect("log-changed", lambda *_: self._fill_recents())
        app.contacts.connect("changed", lambda *_: (self._fill_recents(), self._fill_favs()))
        app.link.connect("state-changed", lambda *_: self._sims())
        self._fill_recents()
        self._fill_favs()
        self._sims()

    def shown(self, number=None, **_):
        if number is not None:
            self.entry.set_text(number)
        self.entry.grab_focus()
        self.entry.set_position(-1)

    # ---- dialer ----

    def _press(self, ch, replace_last=None):
        t = self.entry.get_text()
        if replace_last and t.endswith(replace_last):
            t = t[:-1]
        self.entry.set_text(t + ch)
        self.entry.set_position(-1)
        if self.app.calls.calls and ch in "0123456789*#":
            self.app.calls.dtmf(ch)

    def _backspace(self):
        self.entry.set_text(self.entry.get_text()[:-1])
        self.entry.set_position(-1)

    def _key(self, _c, keyval, _code, _state):
        if self.entry.has_focus():
            return False
        ch = chr(Gdk.keyval_to_unicode(keyval) or 0)
        if ch and (ch.isalnum() or ch in "+*#"):
            self.entry.grab_focus()
            self._press(ch)
            return True
        if keyval == Gdk.KEY_BackSpace:
            self._backspace()
            return True
        return False

    def _suggest(self):
        clear(self.suggest)
        text = self.entry.get_text().strip()
        self._best = None
        if not text:
            self.match_label.set_text("")
            return
        is_number = all(c.isdigit() or c in "+ -()*#" for c in text)
        rows = self.app.contacts.t9_search(text) if is_number else self.app.contacts.search(text, 20)
        # Recent callers who aren't contacts still deserve a match.
        seen = set()
        n = 0
        for c in rows:
            for p in json.loads(c["phones"]):
                if n >= 12:
                    break
                d = digits(p["n"])
                if d in seen:
                    continue
                seen.add(d)
                self._suggestion_row(c["name"], p, c["id"])
                n += 1
        if is_number and len(digits(text)) >= 3:
            for r in self.app.store.q("SELECT DISTINCT number, name FROM calllog WHERE number LIKE ? LIMIT 5", (f"%{digits(text)}%",)):
                if digits(r["number"]) not in seen:
                    seen.add(digits(r["number"]))
                    self._suggestion_row(r["name"] or pretty(r["number"]), {"n": r["number"], "type": "recent"}, None)
        first = self.suggest.get_row_at_index(0)
        self._best = first.number if first and not is_number else None
        self.match_label.set_text(f"Enter calls {first.title}" if self._best else "")

    def _suggestion_row(self, name, p, cid):
        row = Adw.ActionRow(title=GLib.markup_escape_text(name), subtitle=f"{pretty(p['n'])} · {p['type']}", activatable=True)
        row.add_prefix(avatar(name, 32, self.app.contacts.photo(cid) if cid else None))
        row.number = p["n"]
        row.title = name
        btn = Gtk.Button(icon_name="call-start-symbolic", css_classes=["flat", "circular"], valign=Gtk.Align.CENTER)
        btn.connect("clicked", lambda *_: self.app.calls.dial(p["n"], self._account()))
        row.add_suffix(btn)
        self.suggest.append(row)

    def _pick(self, row):
        self.entry.set_text(row.number)
        self.entry.set_position(-1)

    def _call_entry(self):
        text = self.entry.get_text().strip()
        number = self._best or text
        if not number:
            # Like a phone: an empty dial re-fills the last number.
            last = self.app.store.q("SELECT number FROM calllog WHERE type='out' ORDER BY date DESC LIMIT 1")
            if last:
                self.entry.set_text(last[0][0])
            return
        self.app.calls.dial(number, self._account())

    def _sims(self):
        accts = self.app.calls.accounts
        if len(accts) > 1:
            self.sim.set_model(Gtk.StringList.new([a["label"] for a in accts]))
            self.sim.set_selected(next((i for i, a in enumerate(accts) if a.get("default")), 0))
            self.sim.set_visible(True)
        else:
            self.sim.set_visible(False)
        if not accts and self.app.link.online:
            GLib.timeout_add(1500, lambda: self._sims() and False)

    def _account(self):
        accts = self.app.calls.accounts
        if len(accts) > 1:
            return accts[self.sim.get_selected()]["id"]
        return None

    # ---- recents ----

    def _fill_recents(self):
        clear(self.recents)
        f = self.filter.get_active_name()
        last_day = None
        for r in self.app.calls.log(400):
            if f == "missed" and r["type"] not in ("missed", "rejected"):
                continue
            if f in ("in", "out") and r["type"] != f:
                continue
            day = GLib.DateTime.new_from_unix_local(r["date"] / 1000).format("%Y%m%d")
            if day != last_day:
                last_day = day
                sep = Gtk.ListBoxRow(activatable=False, selectable=False)
                sep.set_child(label(day_label(r["date"]).upper(), css=("eyebrow",), xalign=0))
                sep.get_child().set_margin_top(12)
                sep.get_child().set_margin_start(8)
                self.recents.append(sep)
            self.recents.append(self._recent_row(r))

    def _recent_row(self, r):
        cid, cname = self.app.contacts.lookup(r["number"])
        name = cname or r["name"] or pretty(r["number"]) or "Private number"
        row = Gtk.ListBoxRow()
        row.number = r["number"]
        box = Gtk.Box(spacing=12, margin_top=6, margin_bottom=6, margin_start=6, margin_end=6)
        box.append(avatar(name, 38, self.app.contacts.photo(cid) if cid else None))
        mid = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, hexpand=True, valign=Gtk.Align.CENTER)
        t = label(name, ellipsize=True)
        if r["type"] in ("missed", "rejected"):
            t.add_css_class("error")
        mid.append(t)
        line = Gtk.Box(spacing=6)
        line.append(Gtk.Image(icon_name=TYPE_ICON.get(r["type"], "call-start-symbolic"), pixel_size=12, css_classes=["muted"]))
        sub = TYPE_TEXT.get(r["type"], "Call")
        if r["duration"]:
            sub += f" · {duration(r['duration'])}"
        if cname or r["name"]:
            sub += f" · {pretty(r['number'])}"
        elif r["place"]:
            sub += f" · {r['place']}"
        line.append(label(sub, css=("caption", "muted"), ellipsize=True))
        mid.append(line)
        box.append(mid)
        box.append(label(GLib.DateTime.new_from_unix_local(r["date"] / 1000).format("%H:%M"), css=("caption", "muted")))

        menu = Gtk.MenuButton(icon_name="view-more-symbolic", css_classes=["flat", "circular"], valign=Gtk.Align.CENTER)
        pop = Gtk.Popover()
        pbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, margin_top=4, margin_bottom=4)

        def item(text, cb):
            b = Gtk.Button(label=text, css_classes=["flat"])
            b.get_child().set_xalign(0)
            b.connect("clicked", lambda *_: (pop.popdown(), cb()))
            pbox.append(b)

        item("Call", lambda: self.app.calls.dial(r["number"]))
        item("Send SMS", lambda: self.app.present("messages", sms=r["number"]))
        item("Copy number", lambda: self.app.clipboard.set_local(r["number"]))
        if not cid:
            from .people import edit_contact
            item("Add to contacts", lambda: edit_contact(self.app, self.win, number=r["number"]))
        item("Remove from history", lambda: self.app.calls.delete_log(r["id"]))
        pop.set_child(pbox)
        menu.set_popover(pop)
        box.append(menu)
        row.set_child(box)
        return row

    def _fill_favs(self):
        clear(self.favs)
        favs = [c for c in self.app.contacts.all() if c["starred"]]
        if not favs:
            self.favs.append(label("Star contacts to see them here.", css=("muted",)))
        for c in favs:
            phones = json.loads(c["phones"])
            if not phones:
                continue
            b = Gtk.Button(css_classes=["flat"])
            v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            v.append(avatar(c["name"], 72, self.app.contacts.photo(c["id"])))
            v.append(label(c["name"], xalign=0.5, ellipsize=True))
            v.append(label(pretty(phones[0]["n"]), css=("caption", "muted"), xalign=0.5, ellipsize=True))
            b.set_child(v)
            b.connect("clicked", lambda _b, n=phones[0]["n"]: self.app.calls.dial(n))
            self.favs.append(b)
