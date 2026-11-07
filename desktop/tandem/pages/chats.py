from gi.repository import Adw, GLib, Gtk, Pango

from ..contacts import pretty
from ..messages import chat_key
from ..widgets import avatar, clear, day_label, label, scrolled, when
from .base import Page

WHATSAPP = {"com.whatsapp", "com.whatsapp.w4b"}


class MessagesPage(Page):
    title = "Messages"

    def __init__(self, app, win):
        super().__init__(app, win)
        self.current = None  # ("sms", thread_dict) | ("chat", key) | ("new", number)
        new_btn = Gtk.Button(icon_name="mail-message-new-symbolic", tooltip_text="New SMS")
        new_btn.connect("clicked", lambda *_: self._new_sms())
        self.header.pack_start(new_btn)

        paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL, position=340, shrink_start_child=False)
        # left: conversations
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        top = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, margin_top=10, margin_start=10, margin_end=10, margin_bottom=6)
        self.search = Gtk.SearchEntry(placeholder_text="Search conversations")
        self.search.connect("search-changed", lambda *_: self._fill())
        top.append(self.search)
        self.filter = Adw.ToggleGroup()
        for name, title in (("all", "All"), ("sms", "SMS"), ("wa", "WhatsApp"), ("other", "Other")):
            self.filter.add(Adw.Toggle(name=name, label=title))
        self.filter.set_active_name("all")
        self.filter.connect("notify::active-name", lambda *_: self._fill())
        top.append(self.filter)
        left.append(top)
        self.list = Gtk.ListBox(css_classes=["navigation-sidebar"])
        self.list.connect("row-activated", lambda _l, row: self._open(row.item))
        left.append(scrolled(self.list))
        paned.set_start_child(left)

        # right: conversation
        right = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.conv_head = Gtk.Box(spacing=12, margin_top=10, margin_bottom=10, margin_start=16, margin_end=16)
        right.append(self.conv_head)
        right.append(Gtk.Separator())
        self.bubbles = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, margin_top=12, margin_bottom=12,
                               margin_start=18, margin_end=18, valign=Gtk.Align.END)
        self.scroller = scrolled(self.bubbles)
        self.conv_stack = Gtk.Stack()
        self.conv_stack.add_named(Adw.StatusPage(icon_name="mail-unread-symbolic", title="Pick a conversation",
                                                 description="SMS threads come straight from the phone. WhatsApp and other chats "
                                                             "are rebuilt from their notifications and kept here."), "empty")
        self.conv_stack.add_named(self.scroller, "conv")
        right.append(self.conv_stack)
        compose = Gtk.Box(spacing=8, margin_top=8, margin_bottom=10, margin_start=14, margin_end=14)
        self.entry = Gtk.Entry(hexpand=True, placeholder_text="Write a message")
        self.entry.connect("activate", lambda *_: self._send())
        compose.append(self.entry)
        self.send_btn = Gtk.Button(icon_name="mail-send-symbolic", css_classes=["suggested-action", "circular"])
        self.send_btn.connect("clicked", lambda *_: self._send())
        compose.append(self.send_btn)
        self.compose = compose
        right.append(compose)
        paned.set_end_child(right)
        self.set_body(paned)
        compose.set_visible(False)

        app.messages.connect("changed", lambda *_: self._fill())
        app.messages.connect("chat-updated", self._chat_updated)
        app.messages.connect("sms-updated", lambda *_: self._sms_refresh())
        app.contacts.connect("changed", lambda *_: self._fill())
        self._fill()

    def shown(self, chat=None, sms=None, **_):
        if chat is not None and chat.get("conversation"):
            self._open(("chat", chat_key(chat)))
        elif sms:
            t = self.app.messages.thread_for(sms)
            self._open(("sms", t) if t else ("new", sms))

    # ---- list ----

    def _items(self):
        items = []
        for t in self.app.messages.threads:
            cid, cname = self.app.contacts.lookup(t["address"])
            items.append({"kind": "sms", "ref": t, "title": cname or t.get("name") or pretty(t["address"]),
                          "snippet": t["snippet"], "time": t["date"], "unread": t["unread"], "app": "SMS",
                          "photo": self.app.contacts.photo(cid) if cid else None, "pkg": "sms"})
        for c in self.app.messages.chats():
            items.append({"kind": "chat", "ref": c["key"], "title": c["title"], "snippet": c["last_text"], "time": c["last_time"],
                          "unread": c["unread"] or 0, "app": c["app"], "photo": c["avatar"], "pkg": c["pkg"]})
        f = self.filter.get_active_name()
        if f == "sms":
            items = [i for i in items if i["kind"] == "sms"]
        elif f == "wa":
            items = [i for i in items if i["pkg"] in WHATSAPP]
        elif f == "other":
            items = [i for i in items if i["kind"] == "chat" and i["pkg"] not in WHATSAPP]
        q = self.search.get_text().strip().lower()
        if q:
            items = [i for i in items if q in i["title"].lower() or q in (i["snippet"] or "").lower()]
        return sorted(items, key=lambda i: -(i["time"] or 0))

    def _fill(self):
        clear(self.list)
        for it in self._items():
            row = Gtk.ListBoxRow()
            row.item = (it["kind"], it["ref"])
            box = Gtk.Box(spacing=10, margin_top=6, margin_bottom=6, margin_start=4, margin_end=4)
            box.append(avatar(it["title"], 40, it["photo"]))
            mid = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
            top = Gtk.Box(spacing=6)
            t = label(it["title"], ellipsize=True)
            t.set_hexpand(True)
            if it["unread"]:
                t.add_css_class("heading")
            top.append(t)
            top.append(label(when(it["time"]), css=("caption", "muted")))
            mid.append(top)
            bottom = Gtk.Box(spacing=6)
            s = label((it["snippet"] or "").replace("\n", " "), css=("caption", "muted"), ellipsize=True)
            s.set_hexpand(True)
            bottom.append(s)
            bottom.append(label(it["app"], css=("caption", "muted")))
            if it["unread"]:
                bottom.append(label(str(it["unread"]), css=("badge",), xalign=0.5))
            mid.append(bottom)
            box.append(mid)
            row.set_child(box)
            self.list.append(row)

    # ---- conversation ----

    def _head(self, title, sub, photo=None, number=None):
        clear(self.conv_head)
        self.conv_head.append(avatar(title, 38, photo))
        v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, hexpand=True, valign=Gtk.Align.CENTER)
        v.append(label(title, css=("heading",), ellipsize=True))
        if sub:
            v.append(label(sub, css=("caption", "muted"), ellipsize=True))
        self.conv_head.append(v)
        if number:
            b = Gtk.Button(icon_name="call-start-symbolic", tooltip_text="Call", css_classes=["flat", "circular"])
            b.connect("clicked", lambda *_: self.app.calls.dial(number))
            self.conv_head.append(b)

    def _open(self, item):
        self.current = item
        kind, ref = item
        self.conv_stack.set_visible_child_name("conv")
        self.compose.set_visible(True)
        self.entry.set_sensitive(True)
        self.entry.set_placeholder_text("Write a message")
        if kind == "sms":
            cid, cname = self.app.contacts.lookup(ref["address"])
            self._head(cname or ref.get("name") or pretty(ref["address"]), pretty(ref["address"]) + " · SMS",
                       self.app.contacts.photo(cid) if cid else None, ref["address"])
            self._load_sms(ref)
        elif kind == "new":
            cid, cname = self.app.contacts.lookup(ref)
            self._head(cname or pretty(ref), "New SMS", self.app.contacts.photo(cid) if cid else None, ref)
            clear(self.bubbles)
        else:
            chat = self.app.messages.chat(ref)
            if not chat:
                return
            self._head(chat["title"], chat["app"] + (" · group" if chat["is_group"] else ""), chat["avatar"])
            self.app.messages.mark_read(ref)
            if not chat["notif_key"]:
                self.entry.set_sensitive(False)
                self.entry.set_placeholder_text("Replies work while the chat has a notification on the phone")
            self._render([{"text": m["text"], "time": m["time"], "self": bool(m["self"]),
                           "sender": m["sender"] if chat["is_group"] and not m["self"] else ""}
                          for m in self.app.messages.chat_messages(ref)])
        self.entry.grab_focus()

    def _load_sms(self, thread):
        def got(ok, msgs):
            if ok and self.current and self.current[0] == "sms" and self.current[1]["thread"] == thread["thread"]:
                self._render([{"text": m["body"], "time": m["date"], "self": m["type"] != 1,
                               "failed": m["type"] == 5, "queued": m["type"] in (4, 6)} for m in msgs])
        self.app.messages.sms_messages(thread["thread"], got)

    def _render(self, msgs):
        clear(self.bubbles)
        last_day = None
        for m in msgs:
            day = GLib.DateTime.new_from_unix_local(m["time"] / 1000).format("%Y%m%d") if m["time"] else None
            if day != last_day:
                last_day = day
                self.bubbles.append(label(day_label(m["time"]) if m["time"] else "", css=("day-sep",), xalign=0.5))
            self.bubbles.append(self._bubble(m))
        GLib.timeout_add(60, self._scroll_end)

    def _bubble(self, m):
        b = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, css_classes=["bubble"] + (["self"] if m["self"] else []),
                    halign=Gtk.Align.END if m["self"] else Gtk.Align.START)
        if m.get("sender"):
            b.append(label(m["sender"], css=("sender",)))
        t = label(m["text"], wrap=True, selectable=True)
        t.set_max_width_chars(60)
        t.set_wrap_mode(Pango.WrapMode.WORD_CHAR)
        b.append(t)
        meta = GLib.DateTime.new_from_unix_local(m["time"] / 1000).format("%H:%M") if m["time"] else "sending…"
        if m.get("failed"):
            meta += " · failed"
        elif m.get("queued"):
            meta += " · sending"
        b.append(label(meta, css=("meta",), xalign=1 if m["self"] else 0))
        return b

    def _scroll_end(self):
        adj = self.scroller.get_vadjustment()
        adj.set_value(adj.get_upper())
        return False

    def _chat_updated(self, _m, key):
        if self.current == ("chat", key):
            self._open(self.current)

    def _sms_refresh(self):
        if not self.current:
            return
        kind, ref = self.current
        if kind == "sms":
            t = next((t for t in self.app.messages.threads if t["thread"] == ref["thread"]), None)
            if t:
                self.current = ("sms", t)
                self._load_sms(t)
        elif kind == "new":
            t = self.app.messages.thread_for(ref)
            if t:
                self._open(("sms", t))

    def _send(self):
        text = self.entry.get_text().strip()
        if not text or not self.current:
            return
        kind, ref = self.current
        self.entry.set_text("")
        if kind in ("sms", "new"):
            address = ref["address"] if kind == "sms" else ref
            self.bubbles.append(self._bubble({"text": text, "time": 0, "self": True}))
            GLib.timeout_add(60, self._scroll_end)
            self.app.messages.send_sms(address, text, lambda ok, r: ok or self.app.toast(f"SMS failed: {r}"))
        else:
            def done(ok, r):
                if not ok:
                    self.app.toast(str(r))
                    self.entry.set_text(text)
            self.app.messages.reply(ref, text, done)

    def _new_sms(self):
        d = Adw.AlertDialog(heading="New SMS", body="Type a name or number")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        entry = Gtk.SearchEntry(placeholder_text="Name or number")
        results = Gtk.ListBox(css_classes=["boxed-list"])
        box.append(entry)
        box.append(results)
        d.set_extra_child(box)
        d.add_response("cancel", "Cancel")
        d.add_response("go", "Write")
        d.set_response_appearance("go", Adw.ResponseAppearance.SUGGESTED)
        chosen = {"n": None}

        def refresh(*_):
            clear(results)
            for c in self.app.contacts.search(entry.get_text(), 6):
                import json
                for p in json.loads(c["phones"])[:2]:
                    row = Adw.ActionRow(title=GLib.markup_escape_text(c["name"]), subtitle=pretty(p["n"]), activatable=True)
                    row.number = p["n"]
                    results.append(row)

        def pick(_l, row):
            chosen["n"] = row.number
            d.response("go")
            d.close()

        entry.connect("search-changed", refresh)
        results.connect("row-activated", pick)

        def resp(_d, r):
            n = chosen["n"] or entry.get_text().strip()
            if r == "go" and n:
                self.shown(sms=n)
        d.connect("response", resp)
        d.present(self.win)
