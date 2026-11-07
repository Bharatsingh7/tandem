import base64

from gi.repository import Adw, GLib, Gtk

from .. import link as linkmod
from ..widgets import clamp, label, scrolled, texture
from .base import Page

TILES = [
    ("bluetooth", "Bluetooth", "bluetooth-active-symbolic"),
    ("wifi", "Wi-Fi", "network-wireless-symbolic"),
    ("hotspot", "Hotspot", "network-wireless-hotspot-symbolic"),
    ("data", "Mobile data", "network-cellular-symbolic"),
    ("airplane", "Airplane", "airplane-mode-symbolic"),
    ("dnd", "Do not disturb", "notifications-disabled-symbolic"),
    ("location", "Location", "find-location-symbolic"),
    ("torch", "Torch", "weather-clear-symbolic"),
]
NEEDS_HELPER = {"bluetooth", "wifi", "hotspot", "data", "airplane", "location"}


class HomePage(Page):
    title = "Home"

    def __init__(self, app, win):
        super().__init__(app, win)
        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE)
        self.stack.add_named(self.offline_page(), "offline")
        self.stack.add_named(self._pairing_view(), "pairing")
        self.stack.add_named(self._mismatch_view(), "mismatch")
        self.stack.add_named(self._dashboard(), "online")
        self.set_body(self.stack)
        self._updating = False
        self._media = {}
        app.link.connect("state-changed", lambda *_: self._link())
        app.link.connect("event", self._on_event)
        GLib.timeout_add_seconds(1, self._tick)
        self._link()

    # ---- views ----

    def _pairing_view(self):
        sp = Adw.StatusPage(icon_name="phone-symbolic", title="Pair with your phone")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, halign=Gtk.Align.CENTER)
        self.code = label("", css=("display",), xalign=0.5)
        self.code.add_css_class("mono")
        box.append(self.code)
        box.append(label("Tap Accept on the phone if it shows the same code.", css=("muted",), xalign=0.5))
        sp.set_child(box)
        return sp

    def _mismatch_view(self):
        sp = Adw.StatusPage(icon_name="dialog-warning-symbolic", title="This isn't the phone you paired",
                            description="The phone's identity changed (reinstall or a different device). "
                                        "Forget it to pair again.")
        b = Gtk.Button(label="Forget and re-pair", halign=Gtk.Align.CENTER, css_classes=["pill", "destructive-action"])
        b.connect("clicked", lambda *_: self.app.link.forget())
        sp.set_child(b)
        return sp

    def _dashboard(self):
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=22, margin_top=24, margin_bottom=32,
                        margin_start=24, margin_end=24)
        self.helper_banner = Adw.Banner(title="Plug the phone in over USB once to enable Bluetooth, hotspot and other toggles",
                                        revealed=False)
        outer.append(self.helper_banner)

        head = Gtk.Box(spacing=18)
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
        left.append(label("YOUR PHONE", css=("eyebrow",)))
        self.name = label("", css=("display",))
        left.append(self.name)
        self.subline = label("", css=("muted",))
        left.append(self.subline)
        head.append(left)
        bat = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, halign=Gtk.Align.END, valign=Gtk.Align.CENTER)
        self.battery = label("", css=("battery-big",), xalign=1)
        bat.append(self.battery)
        self.battery_sub = label("", css=("muted", "caption"), xalign=1)
        bat.append(self.battery_sub)
        head.append(bat)
        outer.append(head)

        outer.append(label("QUICK SETTINGS", css=("eyebrow",)))
        self.flow = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, column_spacing=10, row_spacing=10,
                                homogeneous=True, max_children_per_line=4, min_children_per_line=2)
        self.tiles = {}
        for key, title, icon in TILES:
            btn = Gtk.Button(css_classes=["toggle-tile", "flat"])
            v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            v.append(Gtk.Image(icon_name=icon, halign=Gtk.Align.START, pixel_size=20))
            v.append(label(title, css=("tile-title",)))
            sub = label("", css=("tile-sub",), ellipsize=True)
            v.append(sub)
            btn.set_child(v)
            btn.sub = sub
            btn.connect("clicked", lambda _b, k=key: self._toggle(k))
            self.tiles[key] = btn
            self.flow.append(btn)
        outer.append(self.flow)

        cols = Gtk.Box(spacing=18, homogeneous=True)
        cols.append(self._sound_card())
        cols.append(self._media_card())
        outer.append(cols)

        outer.append(label("ACTIONS", css=("eyebrow",)))
        acts = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, column_spacing=10, row_spacing=10, max_children_per_line=6)
        self.ring_btn = self._action(acts, "audio-volume-high-symbolic", "Find my phone", self._ring)
        self._action(acts, "system-lock-screen-symbolic", "Lock phone", lambda: self.app.link.call("phone.lock", cb=self._err))
        self._action(acts, "edit-paste-symbolic", "Send clipboard", self._send_clip)
        self._action(acts, "document-send-symbolic", "Send files", self._send_files)
        self._action(acts, "send-to-symbolic", "Open link on phone", self._open_link)
        outer.append(acts)
        return scrolled(clamp(outer, 920))

    def _action(self, flow, icon, text, cb):
        b = Gtk.Button(css_classes=["pill"])
        content = Adw.ButtonContent(icon_name=icon, label=text)
        b.set_child(content)
        b.connect("clicked", lambda *_: cb())
        flow.append(b)
        return content

    def _sound_card(self):
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, css_classes=["card-soft"])
        card.append(label("SOUND", css=("eyebrow",)))
        self.ringer = Adw.ToggleGroup()
        for name, title in (("normal", "Ring"), ("vibrate", "Vibrate"), ("silent", "Silent")):
            self.ringer.add(Adw.Toggle(name=name, label=title))
        self.ringer.connect("notify::active-name", self._ringer_changed)
        card.append(self.ringer)
        self.sliders = {}
        for stream, title in (("media", "Media"), ("ring", "Ring"), ("alarm", "Alarm")):
            row = Gtk.Box(spacing=10)
            row.append(label(title, css=("caption",)))
            sc = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, hexpand=True, draw_value=False,
                           adjustment=Gtk.Adjustment(lower=0, upper=15, step_increment=1, page_increment=1))
            sc.connect("value-changed", self._vol_changed, stream)
            row.get_first_child().set_size_request(56, -1)
            row.append(sc)
            self.sliders[stream] = sc
            card.append(row)
        row = Gtk.Box(spacing=10)
        bl = label("Bright", css=("caption",))
        bl.set_size_request(56, -1)
        row.append(bl)
        self.bright = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, hexpand=True, draw_value=False,
                                adjustment=Gtk.Adjustment(lower=1, upper=255, value=128, step_increment=5))
        self.bright.connect("value-changed", self._bright_changed)
        row.append(self.bright)
        auto = Gtk.Button(label="Auto", css_classes=["flat", "caption"])
        auto.connect("clicked", lambda *_: self.app.link.call("brightness.set", {"auto": True}, self._err))
        row.append(auto)
        card.append(row)
        return card

    def _media_card(self):
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, css_classes=["card-soft"])
        card.append(label("NOW PLAYING", css=("eyebrow",)))
        row = Gtk.Box(spacing=14)
        self.art = Gtk.Picture(content_fit=Gtk.ContentFit.COVER, css_classes=["now-playing-art"])
        self.art.set_size_request(96, 96)
        frame = Gtk.Box()
        frame.set_size_request(96, 96)
        frame.append(self.art)
        row.append(frame)
        info = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3, valign=Gtk.Align.CENTER, hexpand=True)
        self.m_title = label("Nothing playing", css=("display-sm",), ellipsize=True)
        self.m_artist = label("", css=("muted",), ellipsize=True)
        self.m_app = label("", css=("caption", "muted"), ellipsize=True)
        for w in (self.m_title, self.m_artist, self.m_app):
            info.append(w)
        row.append(info)
        card.append(row)
        self.m_progress = Gtk.ProgressBar()
        card.append(self.m_progress)
        ctl = Gtk.Box(spacing=8, halign=Gtk.Align.CENTER)
        for icon, cmd in (("media-skip-backward-symbolic", "prev"), (None, "toggle"), ("media-skip-forward-symbolic", "next")):
            b = Gtk.Button(icon_name=icon or "media-playback-start-symbolic", css_classes=["circular", "flat"] if icon else ["circular", "suggested-action"])
            b.connect("clicked", lambda _b, c=cmd: self.app.link.call("media.cmd", {"cmd": c}, self._err))
            if cmd == "toggle":
                self.play_btn = b
            ctl.append(b)
        card.append(ctl)
        return card

    # ---- updates ----

    def _link(self):
        link = self.app.link
        if link.state == "pairing":
            self.code.set_text(" ".join([link.pair_code[:3], link.pair_code[3:]]) if link.pair_code else "")
            self.stack.set_visible_child_name("pairing")
        elif link.state == "mismatch":
            self.stack.set_visible_child_name("mismatch")
        elif link.online:
            self.stack.set_visible_child_name("online")
            link.call("media.get", cb=lambda ok, r: ok and self._set_media(r))
        else:
            self.stack.set_visible_child_name("offline")
        self.status_changed()

    def _on_event(self, _l, name, d):
        if name == "media":
            self._set_media(d)

    def status_changed(self):
        s = self.app.status
        if not s or not self.app.link.online:
            return
        self._updating = True
        self.name.set_text(s.get("model", ""))
        net = s.get("network", {})
        bars = "▂▄▆█"[: max(1, net.get("signal", 0))]
        self.subline.set_text(f"Android {s.get('android', '')} · {net.get('carrier', '')} {net.get('type', '')} {bars} · "
                              f"{_gb(s['storage']['free'])} free of {_gb(s['storage']['total'])}")
        b = s["battery"]
        self.battery.set_text(f"{b['level']}%")
        self.battery_sub.set_text(("Charging · " if b["charging"] else "Plugged in · " if b["plugged"] else "") + f"{b['temp']:.0f}°C")
        helper = s.get("helper", False)
        self.helper_banner.set_revealed(not helper)
        for key, btn in self.tiles.items():
            on = s["toggles"].get(key, False)
            (btn.add_css_class if on else btn.remove_css_class)("on")
            btn.set_sensitive(helper or key not in NEEDS_HELPER)
            btn.sub.set_text("On" if on else "Off")
        if s["toggles"].get("wifi") is False and s["toggles"].get("hotspot"):
            self.tiles["hotspot"].sub.set_text("On · sharing data")
        self.ringer.set_active_name(s.get("ringer", "normal"))
        for stream, sc in self.sliders.items():
            v = s["volume"].get(stream)
            if v:
                sc.get_adjustment().set_upper(v["max"])
                sc.set_value(v["v"])
        self._updating = False

    def _set_media(self, m):
        self._media = m
        self._media_at = GLib.get_monotonic_time()
        if not m.get("active"):
            self.m_title.set_text("Nothing playing")
            self.m_artist.set_text("")
            self.m_app.set_text("")
            self.art.set_paintable(None)
            self.m_progress.set_fraction(0)
            return
        self.m_title.set_text(m.get("title") or "Unknown")
        self.m_artist.set_text(m.get("artist") or "")
        self.m_app.set_text(m.get("app") or "")
        if m.get("art"):
            self.art.set_paintable(texture(base64.b64decode(m["art"])))
        self.play_btn.set_icon_name("media-playback-pause-symbolic" if m.get("state") == "playing" else "media-playback-start-symbolic")
        self._tick()

    def _tick(self):
        m = self._media
        if m.get("active") and m.get("duration"):
            pos = m.get("position", 0)
            if m.get("state") == "playing":
                pos += (GLib.get_monotonic_time() - self._media_at) // 1000
            self.m_progress.set_fraction(min(1.0, pos / m["duration"]))
        return True

    # ---- actions ----

    def _err(self, ok, r):
        if not ok:
            self.app.toast(str(r))

    def _toggle(self, key):
        on = not self.app.status.get("toggles", {}).get(key, False)
        if key == "hotspot" and not on and self.app.link.transport == "wifi" and \
                self.app.link.host == linkmod.default_gateway():
            d = Adw.AlertDialog(heading="Turn off the hotspot?",
                                body="This PC is online through your phone's hotspot. Turning it off disconnects "
                                     "the PC from the internet and from the phone (until USB or Wi-Fi reconnects it).")
            d.add_response("cancel", "Cancel")
            d.add_response("off", "Turn off")
            d.set_response_appearance("off", Adw.ResponseAppearance.DESTRUCTIVE)
            d.connect("response", lambda _d, r: r == "off" and self.app.set_toggle(key, False))
            d.present(self.win)
            return
        btn = self.tiles[key]
        btn.sub.set_text("Turning on…" if on else "Turning off…")
        self.app.set_toggle(key, on)

    def _ringer_changed(self, group, _p):
        if not self._updating and group.get_active_name():
            self.app.link.call("ringer.set", {"mode": group.get_active_name()}, self._err)

    def _vol_changed(self, sc, stream):
        if self._updating:
            return
        if getattr(sc, "_pending", 0):
            GLib.source_remove(sc._pending)
        def send():
            sc._pending = 0
            self.app.link.call("volume.set", {"stream": stream, "v": int(sc.get_value())}, self._err)
            return False
        sc._pending = GLib.timeout_add(150, send)

    def _bright_changed(self, sc):
        if getattr(self, "_bpending", 0):
            GLib.source_remove(self._bpending)
        def send():
            self._bpending = 0
            self.app.link.call("brightness.set", {"v": int(sc.get_value())}, self._err)
            return False
        self._bpending = GLib.timeout_add(200, send)

    def _ring(self):
        ringing = getattr(self, "_ringing", False)
        self._ringing = not ringing
        self.ring_btn.set_label("Stop ringing" if self._ringing else "Find my phone")
        self.app.link.call("phone.ring", {"on": self._ringing}, self._err)
        if self._ringing:
            GLib.timeout_add_seconds(45, lambda: (self._ringing and self._ring()) and False)

    def _send_clip(self):
        text = self.app.clipboard.send_now()
        self.app.toast("Clipboard sent to phone" if text else "PC clipboard is empty")

    def _send_files(self):
        d = Gtk.FileDialog(title="Send to phone")
        def done(dlg, res):
            try:
                files = dlg.open_multiple_finish(res)
            except Exception:
                return
            paths = [files.get_item(i).get_path() for i in range(files.get_n_items())]
            self.app.toast(f"Sending {len(paths)} file(s)…")
            self.app.transfers.send_files(paths, lambda ok, errs: self.app.toast(
                f"Sent {ok} file(s) to Download/Tandem" + (f" · {errs[0]}" if errs else "")))
        d.open_multiple(self.win, None, done)

    def _open_link(self):
        d = Adw.AlertDialog(heading="Open a link on the phone")
        entry = Gtk.Entry(placeholder_text="https://…", activates_default=True)
        d.set_extra_child(entry)
        d.add_response("cancel", "Cancel")
        d.add_response("open", "Open")
        d.set_default_response("open")
        d.set_response_appearance("open", Adw.ResponseAppearance.SUGGESTED)
        def resp(_d, r):
            url = entry.get_text().strip()
            if r == "open" and url:
                if "://" not in url:
                    url = "https://" + url
                self.app.link.call("phone.openUrl", {"url": url}, self._err)
        d.connect("response", resp)
        d.present(self.win)


def _gb(n):
    return f"{n / 1e9:.0f} GB"
