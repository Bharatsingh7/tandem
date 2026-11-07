"""freedesktop notifications with action buttons (Noctalia, mako, dunst… all speak this)."""
import hashlib

from gi.repository import Gio, GLib

from .config import DATA_DIR

ICON_DIR = DATA_DIR / "icons"


class Notifier:
    def __init__(self):
        self._handlers = {}
        self._bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        self._bus.signal_subscribe(None, "org.freedesktop.Notifications", "ActionInvoked",
                                   "/org/freedesktop/Notifications", None, Gio.DBusSignalFlags.NONE, self._action)
        self._bus.signal_subscribe(None, "org.freedesktop.Notifications", "NotificationClosed",
                                   "/org/freedesktop/Notifications", None, Gio.DBusSignalFlags.NONE, self._closed)

    @staticmethod
    def image_file(data: bytes, ext="png"):
        """Cache image bytes on disk so the notification daemon can show them."""
        ICON_DIR.mkdir(parents=True, exist_ok=True)
        p = ICON_DIR / (hashlib.sha1(data).hexdigest()[:16] + "." + ext)
        if not p.exists():
            p.write_bytes(data)
        return str(p)

    def notify(self, summary, body="", actions=None, on_action=None, image=None, replaces=0,
               urgency=1, category=None, resident=False, timeout=-1):
        """actions: list of (key, label). Returns the notification id via callback-less sync call."""
        flat = []
        for k, label in actions or []:
            flat += [k, label]
        hints = {"urgency": GLib.Variant("y", urgency), "desktop-entry": GLib.Variant("s", "com.cyberwolf.Tandem")}
        if image:
            hints["image-path"] = GLib.Variant("s", image)
        if category:
            hints["category"] = GLib.Variant("s", category)
        if resident:
            hints["resident"] = GLib.Variant("b", True)
        try:
            res = self._bus.call_sync(
                "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                "Notify", GLib.Variant("(susssasa{sv}i)", ("Tandem", replaces, "phone", summary, body, flat, hints, timeout)),
                GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 3000, None)
        except GLib.Error:
            return 0
        nid = res.unpack()[0]
        if on_action:
            self._handlers[nid] = on_action
        return nid

    def close(self, nid):
        if not nid:
            return
        self._handlers.pop(nid, None)
        self._bus.call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                       "CloseNotification", GLib.Variant("(u)", (nid,)), None, Gio.DBusCallFlags.NONE, -1, None, None)

    def _action(self, _c, _s, _p, _i, _sig, params):
        nid, key = params.unpack()
        h = self._handlers.get(nid)
        if h:
            h(key)

    def _closed(self, _c, _s, _p, _i, _sig, params):
        nid, _reason = params.unpack()
        self._handlers.pop(nid, None)
