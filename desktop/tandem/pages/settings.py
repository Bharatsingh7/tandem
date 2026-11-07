import threading

from gi.repository import Adw, GLib, Gtk

from .. import adb
from ..autostart import autostart_enabled, set_autostart
from ..config import cfg, save
from .base import Page


class SettingsPage(Page):
    title = "Settings"

    def __init__(self, app, win):
        super().__init__(app, win)
        page = Adw.PreferencesPage()

        # ---- connection ----
        g = Adw.PreferencesGroup(title="Phone")
        self.conn_row = Adw.ActionRow(title="Connection")
        reconnect = Gtk.Button(label="Reconnect", valign=Gtk.Align.CENTER)
        reconnect.connect("clicked", lambda *_: app.link.reconnect())
        self.conn_row.add_suffix(reconnect)
        g.add(self.conn_row)
        self.fp_row = Adw.ActionRow(title="Phone identity", subtitle_selectable=True)
        g.add(self.fp_row)
        forget = Adw.ButtonRow(title="Forget this phone", css_classes=["destructive-action"])
        forget.connect("activated", lambda *_: self._forget())
        g.add(forget)
        page.add(g)

        # ---- USB setup ----
        g = Adw.PreferencesGroup(title="Setup over USB",
                                 description="With USB debugging on, Tandem grants the phone app every permission it needs and starts "
                                             "the shell helper that controls Bluetooth, hotspot, mobile data and more.")
        self.usb_row = Adw.ActionRow(title="USB")
        g.add(self.usb_row)
        self.helper_row = Adw.ActionRow(title="Shell helper")
        restart = Gtk.Button(label="Restart", valign=Gtk.Align.CENTER)
        restart.connect("clicked", lambda *_: threading.Thread(target=app.link._ensure_helper, daemon=True).start())
        self.helper_row.add_suffix(restart)
        g.add(self.helper_row)
        setup = Adw.ButtonRow(title="Set up phone", start_icon_name="emblem-system-symbolic")
        setup.connect("activated", lambda *_: self._setup())
        g.add(setup)
        page.add(g)

        # ---- calls ----
        g = Adw.PreferencesGroup(title="Calls",
                                 description="The PC becomes a Bluetooth hands-free device for the phone, so you can talk through "
                                             "its microphone and speakers. Calls you answer on the phone stay on the phone.")
        self.bt_row = Adw.ActionRow(title="Bluetooth pairing")
        self.pair_btn = Gtk.Button(label="Pair", valign=Gtk.Align.CENTER, css_classes=["suggested-action"])
        self.pair_btn.connect("clicked", lambda *_: self._pair_bt())
        self.bt_row.add_suffix(self.pair_btn)
        g.add(self.bt_row)
        g.add(self._switch("Use this PC's mic and speakers for calls I take here", "call_audio_on_pc"))
        g.add(self._switch("Pause desktop music during calls", "pause_media_on_call"))
        media = Adw.SwitchRow(title="Play phone media through the PC", subtitle="Bluetooth audio (A2DP) from the phone",
                              active=cfg.get("phone_media_on_pc", False))
        media.connect("notify::active", self._media_audio)
        g.add(media)
        replies = Adw.EntryRow(title="Quick replies (separate with |)", text=" | ".join(cfg.get("quick_replies", [])),
                               show_apply_button=True)
        replies.connect("apply", lambda r: self._set("quick_replies", [x.strip() for x in r.get_text().split("|") if x.strip()]))
        g.add(replies)
        page.add(g)

        # ---- sync ----
        g = Adw.PreferencesGroup(title="Sync")
        g.add(self._switch("Show phone notifications on the desktop", "notify_mirror"))
        g.add(self._switch("Share clipboard both ways", "clipboard_sync",
                           "Copying on the phone needs the “Clipboard to PC” quick tile or the share sheet (an Android rule)"))
        dl = Adw.ActionRow(title="Files from the phone go to", subtitle=cfg["downloads"])
        g.add(dl)
        page.add(g)

        g = Adw.PreferencesGroup(title="Startup")
        auto = Adw.SwitchRow(title="Start Tandem when I log in", subtitle="Runs in the background; open the window any time",
                             active=autostart_enabled())
        auto.connect("notify::active", lambda r, _p: set_autostart(r.get_active()))
        g.add(auto)
        page.add(g)

        self.set_body(page)
        app.link.connect("state-changed", lambda *_: self.refresh())
        app.bt.connect("changed", lambda *_: self.refresh())
        app.bt.connect("passkey", self._passkey)
        app.bt.connect("paired", self._paired)
        self.refresh()

    def _switch(self, title, key, subtitle=None):
        r = Adw.SwitchRow(title=title, active=bool(cfg.get(key)))
        if subtitle:
            r.set_subtitle(subtitle)
        r.connect("notify::active", lambda row, _p: self._set(key, row.get_active()))
        return r

    def _set(self, key, value):
        cfg[key] = value
        save(cfg)

    def shown(self, **_):
        self.refresh()

    def status_changed(self):
        self.refresh()

    def refresh(self):
        link = self.app.link
        self.conn_row.set_subtitle(
            {"online": f"Connected to {link.phone_name} over {'USB' if link.transport == 'usb' else 'Wi-Fi'}",
             "pairing": "Waiting for you to accept on the phone", "mismatch": "Phone identity changed"}.get(
                link.state, "Not connected"))
        fp = cfg.get("phone_fp")
        self.fp_row.set_subtitle(" ".join(fp[i:i + 4] for i in range(0, 24, 4)) if fp else "Not paired")
        self.usb_row.set_subtitle(f"Phone attached ({link.serial})" if link.serial else "No phone on USB")
        self.helper_row.set_subtitle("Running" if self.app.status.get("helper") or link.helper else
                                     "Not running. Plug in USB to start it")
        addr = cfg.get("phone_bt")
        dev = self.app.bt.device(addr) if addr else None
        if not self.app.bt.available:
            self.bt_row.set_subtitle("No Bluetooth adapter on this PC")
            self.pair_btn.set_sensitive(False)
        elif dev and dev.get("Paired"):
            self.bt_row.set_subtitle(f"Paired with {dev.get('Alias', addr)}" + (" · connected" if dev.get("Connected") else ""))
            self.pair_btn.set_label("Re-pair")
        else:
            self.bt_row.set_subtitle("Not paired yet. Needed for talking on the PC")
            self.pair_btn.set_label("Pair")

    def _forget(self):
        d = Adw.AlertDialog(heading="Forget this phone?", body="You'll need to pair again (and accept on the phone).")
        d.add_response("cancel", "Cancel")
        d.add_response("forget", "Forget")
        d.set_response_appearance("forget", Adw.ResponseAppearance.DESTRUCTIVE)
        d.connect("response", lambda _d, r: r == "forget" and self.app.link.forget())
        d.present(self.win)

    def _setup(self):
        serial = self.app.link.serial or (adb.devices() or [None])[0]
        if not serial:
            self.app.toast("Connect the phone with USB debugging enabled (and Tandem installed)")
            return
        self.app.toast("Setting up the phone…")

        def work():
            res = adb.setup_phone(serial)
            bad = [n for n, ok, _ in res if not ok]
            GLib.idle_add(self.app.toast, "Phone set up" if not bad else f"Setup finished; {len(bad)} step(s) failed: {', '.join(bad[:3])}")
            self.app.link._ensure_helper()
        threading.Thread(target=work, daemon=True).start()

    # ---- bluetooth pairing ----

    def _pair_bt(self):
        if not self.app.link.online:
            self.app.toast("Connect the phone first")
            return
        pc = self.app.bt.address()

        def start(*_):
            if not self.app.bt.begin_pairing(120):
                self.app.toast("Couldn't make the PC pairable")
                return
            self.app.toast("Pairing… confirm the code on both screens")
            self.app.link.call("bt.pair", {"address": pc}, lambda ok, r: ok or self.app.toast(f"Pairing failed: {r}"))

        if not self.app.status.get("toggles", {}).get("bluetooth"):
            self.app.toast("Turning on the phone's Bluetooth…")
            self.app.set_toggle("bluetooth", True, lambda ok, r: ok and GLib.timeout_add(2500, lambda: start() and False))
        else:
            start()

    def _passkey(self, _bt, addr, passkey):
        self._pending_addr = addr
        d = Adw.AlertDialog(heading="Confirm pairing", body=f"Make sure the phone shows\n\n<big><b>{passkey:06d}</b></big>\n\nand tap Pair there.",
                            body_use_markup=True)
        d.add_response("ok", "OK")
        d.present(self.win)
        self._passkey_dialog = d

    def _paired(self, _bt, addr):
        cfg["phone_bt"] = addr
        save(cfg)
        if getattr(self, "_passkey_dialog", None):
            self._passkey_dialog.close()
        self.app.calls.push_prefs()
        self.app.bt.connect_phone(addr)
        if not cfg.get("phone_media_on_pc"):
            GLib.timeout_add_seconds(4, lambda: self.app.bt.set_media_audio(addr, False) and False)
        self.app.toast("Paired. Calls can now use this PC's audio")
        self.refresh()

    def _media_audio(self, row, _p):
        self._set("phone_media_on_pc", row.get_active())
        if cfg.get("phone_bt"):
            self.app.bt.set_media_audio(cfg["phone_bt"], row.get_active())
