"""Call state on the PC: ringing popups, answering with PC audio, missed calls, media pausing."""
import subprocess
from pathlib import Path

from gi.repository import GLib, GObject

from . import mpris
from .config import cfg
from .contacts import pretty


class CallManager(GObject.Object):
    __gsignals__ = {
        "changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
        "log-changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
    }

    def __init__(self, app):
        super().__init__()
        self.app = app
        self.link = app.link
        self.calls = []
        self.audio = {}
        self.companion = False
        self.accounts = []
        self._in_call = False
        self._paused = []
        self._ring_nid = 0
        self._ringer_proc = None
        self.window = None
        self.notch = None
        self.link.connect("event", self._on_event)
        self.link.connect("state-changed", self._on_state)

    # ---- state ----

    def _on_state(self, link):
        if link.online:
            link.call("call.list", cb=lambda ok, r: ok and self._update(r))
            link.call("call.accounts", cb=lambda ok, r: ok and setattr(self, "accounts", r))
            self.refresh_log()
            self.push_prefs()
        elif self.calls:
            self._update({"calls": []})

    def push_prefs(self):
        """Tell the phone the PC's Bluetooth address so it can route PC-answered calls here."""
        addr = self.app.bt.address() if self.app.bt.available else None
        if addr and self.link.online:
            self.link.call("call.prefs", {"pcBt": addr})

    def _on_event(self, _l, name, data):
        if name == "call.state":
            self._update(data)
        elif name == "call.logChanged":
            self.refresh_log()

    def refresh_log(self):
        since = self.app.store.get("calllog_since", 0)

        def got(ok, rows):
            if not ok:
                return
            db = self.app.store.db
            for r in rows:
                db.execute("INSERT OR REPLACE INTO calllog VALUES(?,?,?,?,?,?,?,?)",
                           (r["id"], r["number"], r["name"], r["type"], r["date"], r["duration"], r["account"], r["place"]))
            db.commit()
            if rows:
                self.app.store.put("calllog_since", max(r["date"] for r in rows) - 1)
            self.emit("log-changed")

        self.link.call("call.log", {"limit": 500, "since": since}, got)

    def log(self, limit=300):
        return self.app.store.q("SELECT * FROM calllog ORDER BY date DESC LIMIT ?", (limit,))

    def delete_log(self, rid):
        self.app.store.x("DELETE FROM calllog WHERE id=?", (rid,))
        self.link.call("call.logDelete", {"id": rid})
        self.emit("log-changed")

    def _update(self, d):
        self.calls = d.get("calls", [])
        self.audio = d.get("audio") or {}
        self.companion = d.get("companion", False)
        ended = d.get("ended")
        if ended and ended.get("incoming") and ended.get("missed"):
            self._missed(ended)

        ringing = next((c for c in self.calls if c["state"] == "ringing"), None)
        if ringing:
            self._ring_notify(ringing)
            self._start_ringtone()
        else:
            self._stop_ringtone()
            if self._ring_nid:
                self.app.notifier.close(self._ring_nid)
                self._ring_nid = 0

        if self.calls and not self._in_call:
            self._in_call = True
            if cfg.get("pause_media_on_call"):
                self._paused = mpris.pause_desktop_players()
        elif not self.calls and self._in_call:
            self._in_call = False
            mpris.resume_players(self._paused)
            self._paused = []

        if self.calls:
            self._notch().update()
            # Also update the full CallWindow if it's already visible.
            if self.window and self.window.get_visible():
                self.window.update()
        else:
            if self.notch:
                self.notch.ended()
            if self.window:
                self.window.ended()
        self.emit("changed")

    def _window(self):
        if not self.window:
            from .callwindow import CallWindow
            self.window = CallWindow(self.app)
        return self.window

    def _notch(self):
        if not self.notch:
            from .notchwindow import NotchWindow
            self.notch = NotchWindow(self.app)
        return self.notch

    def display(self, call):
        """(title, subtitle, contact_id) for a call, preferring our synced contacts."""
        cid, name = self.app.contacts.lookup(call.get("number"))
        name = name or call.get("name") or ""
        number = pretty(call.get("number")) or "Unknown number"
        return (name or number, number if name else "", cid if cid else (call.get("contactId") if call.get("contactId", -1) > 0 else None))

    def _ring_notify(self, call):
        title, sub, cid = self.display(call)
        photo = self.app.contacts.photo(cid) if cid else None
        image = self.app.notifier.image_file(photo, "jpg") if photo else None

        def act(key):
            if key == "answer":
                self.answer(call["id"])
            elif key == "decline":
                self.reject(call["id"])
            else:
                self.app.present("calls")

        self._ring_nid = self.app.notifier.notify(
            f"Incoming call · {title}", sub, [("answer", "Answer on PC"), ("decline", "Decline")], act,
            image=image, replaces=self._ring_nid, urgency=2, category="call.incoming", resident=True, timeout=0)

    def _missed(self, call):
        title, sub, cid = self.display(call)
        photo = self.app.contacts.photo(cid) if cid else None
        number = call.get("number")

        def act(key):
            if key == "call":
                self.dial(number)
            elif key == "sms":
                self.app.present("messages", sms=number)

        self.app.notifier.notify(f"Missed call · {title}", sub or GLib.DateTime.new_now_local().format("%H:%M"),
                                 [("call", "Call back"), ("sms", "Message")] if number else [], act,
                                 image=self.app.notifier.image_file(photo, "jpg") if photo else None, category="call.unanswered")

    # ---- actions ----

    def _ensure_pc_audio(self):
        phone_bt = cfg.get("phone_bt")
        if cfg.get("call_audio_on_pc") and phone_bt and self.app.bt.available:
            self.app.bt.connect_phone(phone_bt)
            return True
        return False

    def answer(self, cid=None):
        pc = self._ensure_pc_audio()
        self.link.call("call.answer", {"id": cid, "pc": pc} if cid else {"pc": pc}, self._toast_err)

    def reject(self, cid=None, text=None):
        args = {"id": cid} if cid else {}
        if text:
            args["text"] = text
        self.link.call("call.reject", args, self._toast_err)

    def end(self, cid=None):
        self.link.call("call.end", {"id": cid} if cid else {}, self._toast_err)

    def hold(self, cid, on):
        self.link.call("call.hold", {"id": cid, "on": on}, self._toast_err)

    def swap(self):
        self.link.call("call.swap", {}, self._toast_err)

    def merge(self, cid):
        self.link.call("call.merge", {"id": cid}, self._toast_err)

    def mute(self, on):
        self.link.call("call.mute", {"on": on}, self._toast_err)

    def dtmf(self, digits, cid=None):
        self.link.call("call.dtmf", {"digits": digits, **({"id": cid} if cid else {})})

    def route(self, to, cid=None):
        """to: "pc" | "phone" | "speaker"."""
        if to == "pc":
            self._ensure_pc_audio()
        self.link.call("call.route", {"to": "earpiece" if to == "phone" else to, **({"id": cid} if cid else {})}, self._toast_err)

    def dial(self, number, account=None):
        if not number:
            return
        if not self.link.online:
            self.app.toast("Phone not connected")
            return
        pc = self._ensure_pc_audio()
        args = {"number": number, "pc": pc}
        if account:
            args["account"] = account
        self.link.call("call.dial", args, self._toast_err)

    def _toast_err(self, ok, r):
        if not ok:
            self.app.toast(str(r))

    def _start_ringtone(self):
        if self._ringer_proc and self._ringer_proc.poll() is None:
            return
        sound_file = Path.home() / ".local/share/sounds/scifi-calm/stereo/phone-incoming-call.oga"
        if not sound_file.exists():
            sound_file = Path("/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga")
        if not sound_file.exists():
            return
        cmd = ["pw-play", str(sound_file)] if Path("/usr/bin/pw-play").exists() else ["canberra-gtk-play", "-f", str(sound_file)]
        try:
            self._ringer_proc = subprocess.Popen(cmd)

            def check_loop():
                if any(c.get("state") == "ringing" for c in self.calls):
                    self._start_ringtone()
                return False

            GLib.timeout_add(4500, check_loop)
        except Exception:
            pass

    def _stop_ringtone(self):
        if self._ringer_proc:
            try:
                self._ringer_proc.terminate()
            except Exception:
                pass
            self._ringer_proc = None
