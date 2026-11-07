"""Phone media exposed as an MPRIS player, so the desktop's media widgets and keys control it."""
import base64

from gi.repository import Gio, GLib

from .notifier import Notifier

XML = """
<node>
  <interface name="org.mpris.MediaPlayer2">
    <method name="Raise"/><method name="Quit"/>
    <property name="CanQuit" type="b" access="read"/><property name="CanRaise" type="b" access="read"/>
    <property name="HasTrackList" type="b" access="read"/><property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/><property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <method name="Next"/><method name="Previous"/><method name="Pause"/><method name="PlayPause"/>
    <method name="Stop"/><method name="Play"/>
    <method name="Seek"><arg direction="in" name="Offset" type="x"/></method>
    <method name="SetPosition"><arg direction="in" name="TrackId" type="o"/><arg direction="in" name="Position" type="x"/></method>
    <method name="OpenUri"><arg direction="in" name="Uri" type="s"/></method>
    <signal name="Seeked"><arg name="Position" type="x"/></signal>
    <property name="PlaybackStatus" type="s" access="read"/><property name="Rate" type="d" access="read"/>
    <property name="Metadata" type="a{sv}" access="read"/><property name="Volume" type="d" access="read"/>
    <property name="Position" type="x" access="read"/><property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/><property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/><property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/><property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
  </interface>
</node>
"""
PATH = "/org/mpris/MediaPlayer2"


class Mpris:
    def __init__(self, link, raise_cb):
        self.link = link
        self.raise_cb = raise_cb
        self.m = {"active": False}
        self._art = ""
        self._at = GLib.get_monotonic_time()
        self._conn = None
        self._owner = 0
        self._node = Gio.DBusNodeInfo.new_for_xml(XML)
        link.connect("event", self._on_event)
        link.connect("state-changed", self._on_state)

    def _on_state(self, link):
        if link.online:
            link.call("media.get", cb=lambda ok, r: ok and self.update(r))
        else:
            self.update({"active": False})

    def _on_event(self, _l, name, data):
        if name == "media":
            self.update(data)

    def update(self, m):
        self.m = m
        self._at = GLib.get_monotonic_time()
        if m.get("art"):
            self._art = "file://" + Notifier.image_file(base64.b64decode(m["art"]), "jpg")
        elif not m.get("active"):
            self._art = ""
        if m.get("active") and not self._owner:
            self._owner = Gio.bus_own_name(Gio.BusType.SESSION, "org.mpris.MediaPlayer2.tandem",
                                           Gio.BusNameOwnerFlags.NONE, self._acquired, None, None)
        elif not m.get("active") and self._owner:
            Gio.bus_unown_name(self._owner)
            self._owner = 0
            self._conn = None
            return
        if self._conn:
            changed = {k: self._prop("org.mpris.MediaPlayer2.Player", k) for k in
                       ("PlaybackStatus", "Metadata", "CanGoNext", "CanGoPrevious", "CanSeek")}
            self._conn.emit_signal(None, PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                   GLib.Variant("(sa{sv}as)", ("org.mpris.MediaPlayer2.Player", changed, [])))

    def position_ms(self):
        pos = self.m.get("position", 0)
        if self.m.get("state") == "playing":
            pos += (GLib.get_monotonic_time() - self._at) // 1000
        return min(pos, self.m.get("duration") or pos)

    def _acquired(self, conn, _name):
        self._conn = conn
        for iface in self._node.interfaces:
            conn.register_object(PATH, iface, self._call, self._get, None)

    def _call(self, _c, _s, _p, iface, method, params, inv):
        cmd = {"Next": "next", "Previous": "prev", "Pause": "pause", "PlayPause": "toggle", "Stop": "pause", "Play": "play"}
        if method in cmd:
            self.link.call("media.cmd", {"cmd": cmd[method]})
        elif method == "Seek":
            self.link.call("media.cmd", {"cmd": "seek", "pos": max(0, self.position_ms() + params.unpack()[0] // 1000)})
        elif method == "SetPosition":
            self.link.call("media.cmd", {"cmd": "seek", "pos": params.unpack()[1] // 1000})
        elif method == "Raise":
            self.raise_cb()
        inv.return_value(None)

    def _get(self, _c, _s, _p, iface, prop):
        return self._prop(iface, prop)

    def _prop(self, iface, prop):
        m = self.m
        if iface == "org.mpris.MediaPlayer2":
            return {
                "CanQuit": GLib.Variant("b", False), "CanRaise": GLib.Variant("b", True),
                "HasTrackList": GLib.Variant("b", False),
                "Identity": GLib.Variant("s", f"{m.get('app') or 'Phone'} (phone)"),
                "DesktopEntry": GLib.Variant("s", "com.cyberwolf.Tandem"),
                "SupportedUriSchemes": GLib.Variant("as", []), "SupportedMimeTypes": GLib.Variant("as", []),
            }.get(prop)
        meta = {
            "mpris:trackid": GLib.Variant("o", f"/com/cyberwolf/tandem/track/{abs(m.get('artKey', 0))}"),
            "xesam:title": GLib.Variant("s", m.get("title") or ""),
            "xesam:artist": GLib.Variant("as", [m.get("artist") or ""]),
            "xesam:album": GLib.Variant("s", m.get("album") or ""),
            "mpris:length": GLib.Variant("x", (m.get("duration") or 0) * 1000),
        }
        if self._art:
            meta["mpris:artUrl"] = GLib.Variant("s", self._art)
        return {
            "PlaybackStatus": GLib.Variant("s", {"playing": "Playing", "paused": "Paused"}.get(m.get("state"), "Stopped")),
            "Rate": GLib.Variant("d", 1.0), "MinimumRate": GLib.Variant("d", 1.0), "MaximumRate": GLib.Variant("d", 1.0),
            "Metadata": GLib.Variant("a{sv}", meta), "Volume": GLib.Variant("d", 1.0),
            "Position": GLib.Variant("x", self.position_ms() * 1000),
            "CanGoNext": GLib.Variant("b", bool(m.get("canNext", True))),
            "CanGoPrevious": GLib.Variant("b", bool(m.get("canPrev", True))),
            "CanPlay": GLib.Variant("b", True), "CanPause": GLib.Variant("b", True),
            "CanSeek": GLib.Variant("b", bool(m.get("canSeek"))), "CanControl": GLib.Variant("b", True),
        }.get(prop)


def pause_desktop_players(own="org.mpris.MediaPlayer2.tandem"):
    """Pause every playing desktop MPRIS player (not the phone's); returns the names to resume later."""
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    names = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
                          None, GLib.VariantType("(as)"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
    paused = []
    for n in names:
        if not n.startswith("org.mpris.MediaPlayer2.") or n == own:
            continue
        try:
            st = bus.call_sync(n, PATH, "org.freedesktop.DBus.Properties", "Get",
                               GLib.Variant("(ss)", ("org.mpris.MediaPlayer2.Player", "PlaybackStatus")),
                               GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 1000, None).unpack()[0]
            if st == "Playing":
                bus.call_sync(n, PATH, "org.mpris.MediaPlayer2.Player", "Pause", None, None, Gio.DBusCallFlags.NONE, 1000, None)
                paused.append(n)
        except GLib.Error:
            pass
    return paused


def resume_players(names):
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    for n in names:
        bus.call(n, PATH, "org.mpris.MediaPlayer2.Player", "Play", None, None, Gio.DBusCallFlags.NONE, 1000, None, None)
