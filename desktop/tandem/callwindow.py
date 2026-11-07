"""Floating call window: incoming (answer / decline / quick reply) and in-call controls."""
from gi.repository import Adw, GLib, Gtk

from .config import cfg
from .widgets import avatar, clear, label

KEYS = "123456789*0#"


class CallWindow(Adw.Window):
    def __init__(self, app):
        super().__init__(title="Call", default_width=380, default_height=600, resizable=False,
                         css_classes=["call-window"])
        self.app = app
        self.cm = app.calls
        self._timer = 0
        self._close_timer = 0
        self._last_shown = None
        self.set_hide_on_close(True)

        view = Adw.ToolbarView()
        view.add_top_bar(Adw.HeaderBar(show_title=False))
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_start=26, margin_end=26, margin_bottom=26)
        view.set_content(self.box)
        self.set_content(view)

    # ---- rendering ----

    def _primary(self):
        calls = self.cm.calls
        for state in ("ringing", "dialing", "active", "holding"):
            for c in calls:
                if c["state"] == state:
                    return c
        return calls[0] if calls else None

    def update(self):
        if self._close_timer:
            GLib.source_remove(self._close_timer)
            self._close_timer = 0
        c = self._primary()
        if not c:
            return
        clear(self.box)
        title, sub, cid = self.cm.display(c)
        self._call = c
        photo = self.app.contacts.photo(cid) if cid else None
        others = [x for x in self.cm.calls if x["id"] != c["id"]]

        if others:
            o = others[0]
            ot, _s, _c = self.cm.display(o)
            strip = Gtk.Box(spacing=8, css_classes=["card-soft"])
            strip.append(label(f"{ot} · {o['state']}", hexpand=True, ellipsize=True))
            sw = Gtk.Button(label="Swap", css_classes=["pill"])
            sw.connect("clicked", lambda *_: self.cm.swap())
            strip.append(sw)
            if c.get("canHold"):
                mg = Gtk.Button(label="Merge", css_classes=["pill"])
                mg.connect("clicked", lambda *_: self.cm.merge(c["id"]))
                strip.append(mg)
            self.box.append(strip)

        av = avatar(title, 128, photo)
        av.set_halign(Gtk.Align.CENTER)
        av.set_margin_top(14)
        self.box.append(av)
        self.box.append(label(title, css=("caller",), xalign=0.5, wrap=True))
        if sub:
            self.box.append(label(sub, css=("muted",), xalign=0.5))
        acct = next((a for a in self.cm.accounts if a["id"] == c.get("account")), None)
        state_text = {"ringing": "Incoming call", "dialing": "Calling…", "active": "", "holding": "On hold"}.get(c["state"], "")
        if acct and len(self.cm.accounts) > 1:
            state_text += f" · {acct['label']}"
        self.timer = label(state_text, css=("timer",), xalign=0.5)
        self.box.append(self.timer)
        self.box.append(Gtk.Box(vexpand=True))

        if c["state"] == "ringing" and c.get("incoming"):
            self._incoming_controls(c)
            self.set_title(f"Incoming call · {title}")
        else:
            self._call_controls(c)
            self.set_title(f"Call · {title}")
        if c["state"] == "active" and not self._timer:
            self._timer = GLib.timeout_add(500, self._tick)
        self._tick()

    def _incoming_controls(self, c):
        note = label("Answering here uses this PC's mic and speakers" if self._pc_audio() else
                     "Answer here, then talk on the phone (pair Bluetooth in Settings to talk on the PC)",
                     css=("caption", "muted"), xalign=0.5, wrap=True)
        self.box.append(note)
        row = Gtk.Box(spacing=36, halign=Gtk.Align.CENTER, margin_top=10)
        row.append(self._round("call-stop-symbolic", "Decline", lambda: self.cm.reject(c["id"]), "call-red", big=True))
        row.append(self._round("call-start-symbolic", "Answer", lambda: self.cm.answer(c["id"]), "call-green", big=True))
        self.box.append(row)

        msg = Gtk.MenuButton(label="Reply with a message", halign=Gtk.Align.CENTER, css_classes=["flat"], margin_top=6)
        pop = Gtk.Popover()
        pb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, margin_top=6, margin_bottom=6, margin_start=6, margin_end=6)
        for text in cfg.get("quick_replies", []):
            b = Gtk.Button(label=text, css_classes=["flat"])
            b.get_child().set_xalign(0)
            b.connect("clicked", lambda _b, t=text: (pop.popdown(), self.cm.reject(c["id"], t)))
            pb.append(b)
        custom = Gtk.Entry(placeholder_text="Write your own…")
        custom.connect("activate", lambda e: e.get_text().strip() and (pop.popdown(), self.cm.reject(c["id"], e.get_text().strip())))
        pb.append(custom)
        pop.set_child(pb)
        msg.set_popover(pop)
        self.box.append(msg)

    def _call_controls(self, c):
        audio = self.cm.audio or {}
        grid = Gtk.Grid(row_spacing=14, column_spacing=22, halign=Gtk.Align.CENTER)
        muted = audio.get("muted", False)
        grid.attach(self._toggle_btn("microphone-disabled-symbolic", "Unmute" if muted else "Mute", muted,
                                     lambda: self.cm.mute(not muted)), 0, 0, 1, 1)
        self.keypad_rev = Gtk.Revealer(transition_type=Gtk.RevealerTransitionType.SLIDE_UP)
        grid.attach(self._toggle_btn("input-dialpad-symbolic", "Keypad", False,
                                     lambda: self.keypad_rev.set_reveal_child(not self.keypad_rev.get_reveal_child())), 1, 0, 1, 1)
        holding = c["state"] == "holding"
        hold = self._toggle_btn("media-playback-pause-symbolic", "Resume" if holding else "Hold", holding,
                                lambda: self.cm.hold(c["id"], not holding))
        hold.set_sensitive(bool(c.get("canHold")))
        grid.attach(hold, 2, 0, 1, 1)
        grid.attach(self._audio_btn(audio), 0, 1, 3, 1)
        self.box.append(grid)

        pad = Gtk.Grid(row_spacing=6, column_spacing=10, halign=Gtk.Align.CENTER, css_classes=["dialpad"], margin_top=8)
        for i, k in enumerate(KEYS):
            b = Gtk.Button(label=k, css_classes=["flat"])
            b.connect("clicked", lambda _b, d=k: self.cm.dtmf(d, c["id"]))
            pad.attach(b, i % 3, i // 3, 1, 1)
        self.keypad_rev.set_child(pad)
        self.box.append(self.keypad_rev)

        end = self._round("call-stop-symbolic", "End call", lambda: self.cm.end(c["id"]), "call-red", big=True)
        end.set_halign(Gtk.Align.CENTER)
        end.set_margin_top(14)
        self.box.append(end)

    def _audio_btn(self, audio):
        route = "pc" if audio.get("onPc") else audio.get("route", "earpiece")
        names = {"pc": "This PC", "earpiece": "Phone", "speaker": "Phone speaker", "bluetooth": "Bluetooth headset",
                 "wired": "Wired headset"}
        mb = Gtk.MenuButton(halign=Gtk.Align.CENTER, css_classes=["pill"])
        mb.set_child(Adw.ButtonContent(icon_name="audio-speakers-symbolic", label=f"Audio: {names.get(route, route)}"))
        pop = Gtk.Popover()
        pb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, margin_top=4, margin_bottom=4)
        options = [("pc", "This PC"), ("phone", "Phone"), ("speaker", "Phone speaker")]
        for key, text in options:
            b = Gtk.Button(label=("✓ " if (key == route or (key == "phone" and route == "earpiece")) else "   ") + text, css_classes=["flat"])
            b.get_child().set_xalign(0)
            b.set_sensitive(key != "pc" or bool(cfg.get("phone_bt")))
            b.connect("clicked", lambda _b, k=key: (pop.popdown(), self.cm.route(k, self._call["id"])))
            pb.append(b)
        pop.set_child(pb)
        mb.set_popover(pop)
        return mb

    def _round(self, icon, tip, cb, css, big=False):
        b = Gtk.Button(icon_name=icon, tooltip_text=tip, css_classes=["call-btn" if big else "round", css])
        b.connect("clicked", lambda *_: cb())
        return b

    def _toggle_btn(self, icon, text, active, cb):
        v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        b = Gtk.Button(icon_name=icon, css_classes=["round"] + (["active"] if active else []), halign=Gtk.Align.CENTER)
        b.connect("clicked", lambda *_: cb())
        v.append(b)
        v.append(label(text, css=("caption",), xalign=0.5))
        return v

    def _pc_audio(self):
        return bool(cfg.get("call_audio_on_pc") and cfg.get("phone_bt"))

    def _tick(self):
        c = getattr(self, "_call", None)
        if not c or c["state"] != "active" or not c.get("since"):
            self._timer = 0 if (not c or c["state"] != "active") else self._timer
            return bool(c and c["state"] == "active")
        secs = max(0, int(GLib.get_real_time() / 1e6 - c["since"] / 1000))
        h, rem = divmod(secs, 3600)
        m, s = divmod(rem, 60)
        self.timer.set_text(f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}")
        return True

    def ended(self):
        if self._timer:
            GLib.source_remove(self._timer)
            self._timer = 0
        if hasattr(self, "timer"):
            self.timer.set_text("Call ended")
        self._last_shown = None
        if not self._close_timer:
            self._close_timer = GLib.timeout_add(1800, self._auto_close)

    def _auto_close(self):
        self._close_timer = 0
        if not self.cm.calls:
            self.set_visible(False)
        return False
