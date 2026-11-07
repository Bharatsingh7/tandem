"""BlueZ plumbing for call audio: the PC acts as the phone's hands-free headset (HFP HF via PipeWire).

Pairing: the PC turns pairable, registers a one-shot agent, and asks the phone to bond with the PC's
address; both sides show the same passkey. After that the phone routes call audio here on request.
"""
from gi.repository import Gio, GLib, GObject

BLUEZ = "org.bluez"
HFP_AG = "0000111f-0000-1000-8000-00805f9b34fb"     # phone side of hands-free
A2DP_SOURCE = "0000110a-0000-1000-8000-00805f9b34fb"  # phone streaming media to us
AGENT_PATH = "/com/cyberwolf/tandem/agent"
AGENT_XML = """
<node><interface name="org.bluez.Agent1">
  <method name="Release"/>
  <method name="RequestPinCode"><arg type="o" direction="in"/><arg type="s" direction="out"/></method>
  <method name="DisplayPinCode"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
  <method name="RequestPasskey"><arg type="o" direction="in"/><arg type="u" direction="out"/></method>
  <method name="DisplayPasskey"><arg type="o" direction="in"/><arg type="u" direction="in"/><arg type="q" direction="in"/></method>
  <method name="RequestConfirmation"><arg type="o" direction="in"/><arg type="u" direction="in"/></method>
  <method name="RequestAuthorization"><arg type="o" direction="in"/></method>
  <method name="AuthorizeService"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
  <method name="Cancel"/>
</interface></node>
"""


def _addr(path):
    return path.rsplit("dev_", 1)[-1].replace("_", ":")


class Bluetooth(GObject.Object):
    __gsignals__ = {
        "changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
        "passkey": (GObject.SignalFlags.RUN_FIRST, None, (str, int)),
        "paired": (GObject.SignalFlags.RUN_FIRST, None, (str,)),
    }

    def __init__(self):
        super().__init__()
        self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM)
        self.adapter = None
        self._agent_reg = 0
        self._pair_timer = 0
        try:
            for path, ifaces in self._objects().items():
                if "org.bluez.Adapter1" in ifaces:
                    self.adapter = path
                    break
        except GLib.Error:
            pass
        self.bus.signal_subscribe(BLUEZ, "org.freedesktop.DBus.Properties", "PropertiesChanged", None, None,
                                  Gio.DBusSignalFlags.NONE, lambda *a: self.emit("changed"))

    @property
    def available(self):
        return self.adapter is not None

    def _objects(self):
        return self.bus.call_sync(BLUEZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", None,
                                  GLib.VariantType("(a{oa{sa{sv}}})"), Gio.DBusCallFlags.NONE, 3000, None).unpack()[0]

    def _prop(self, path, iface, name):
        return self.bus.call_sync(BLUEZ, path, "org.freedesktop.DBus.Properties", "Get", GLib.Variant("(ss)", (iface, name)),
                                  GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 3000, None).unpack()[0]

    def _set(self, path, iface, name, value):
        self.bus.call_sync(BLUEZ, path, "org.freedesktop.DBus.Properties", "Set", GLib.Variant("(ssv)", (iface, name, value)),
                           None, Gio.DBusCallFlags.NONE, 3000, None)

    def address(self):
        return self._prop(self.adapter, "org.bluez.Adapter1", "Address") if self.adapter else None

    def powered(self):
        return bool(self.adapter and self._prop(self.adapter, "org.bluez.Adapter1", "Powered"))

    def device(self, address):
        """BlueZ properties of a known device, or None."""
        if not address or not self.adapter:
            return None
        path = f"{self.adapter}/dev_{address.upper().replace(':', '_')}"
        try:
            return {"path": path, **self._objects().get(path, {}).get("org.bluez.Device1", {})} if path in self._objects() else None
        except GLib.Error:
            return None

    def connect_phone(self, address, cb=None):
        dev = self.device(address)
        if not dev:
            if cb:
                cb(False, "not paired")
            return

        def done(bus, res):
            try:
                bus.call_finish(res)
                ok, err = True, None
            except GLib.Error as e:
                ok, err = "AlreadyConnected" in e.message, e.message
            if cb:
                cb(ok, err)

        self.bus.call(BLUEZ, dev["path"], "org.bluez.Device1", "ConnectProfile", GLib.Variant("(s)", (HFP_AG,)),
                      None, Gio.DBusCallFlags.NONE, 20000, None, done)

    def set_media_audio(self, address, on):
        """Phone media over A2DP to the PC's speakers, on or off."""
        dev = self.device(address)
        if not dev:
            return
        method = "ConnectProfile" if on else "DisconnectProfile"
        self.bus.call(BLUEZ, dev["path"], "org.bluez.Device1", method, GLib.Variant("(s)", (A2DP_SOURCE,)),
                      None, Gio.DBusCallFlags.NONE, 15000, None, lambda b, r: _swallow(b, r))

    def media_connected(self, address):
        dev = self.device(address)
        if not dev or not dev.get("Connected"):
            return False
        try:
            path = dev["path"]
            for p, ifaces in self._objects().items():
                if p.startswith(path + "/") and "org.bluez.MediaTransport1" in ifaces:
                    return ifaces["org.bluez.MediaTransport1"].get("UUID") == A2DP_SOURCE
        except GLib.Error:
            pass
        return False

    def battery_level(self, address):
        dev = self.device(address)
        if not dev or not self.adapter:
            return None
        path = dev["path"]
        try:
            return self._objects().get(path, {}).get("org.bluez.Battery1", {}).get("Percentage")
        except GLib.Error:
            return None

    def connect_pan(self, address):
        dev = self.device(address)
        if not dev:
            return
        self.bus.call(BLUEZ, dev["path"], "org.bluez.Network1", "Connect", GLib.Variant("(s)", ("nap",)),
                      None, Gio.DBusCallFlags.NONE, 10000, None, lambda b, r: _swallow(b, r))
        import subprocess
        try:
            subprocess.Popen(["nmcli", "con", "up", "id", "motorola edge 60 pro Network"], stderr=subprocess.DEVNULL)
        except Exception:
            pass

    # ---- pairing ----

    def begin_pairing(self, seconds=90):
        """Pairable + discoverable, and an agent that accepts the phone's bond request."""
        if not self.adapter:
            return False
        self._set(self.adapter, "org.bluez.Adapter1", "Powered", GLib.Variant("b", True))
        self._set(self.adapter, "org.bluez.Adapter1", "Pairable", GLib.Variant("b", True))
        self._set(self.adapter, "org.bluez.Adapter1", "DiscoverableTimeout", GLib.Variant("u", seconds))
        self._set(self.adapter, "org.bluez.Adapter1", "Discoverable", GLib.Variant("b", True))
        if not self._agent_reg:
            node = Gio.DBusNodeInfo.new_for_xml(AGENT_XML)
            self._agent_reg = self.bus.register_object(AGENT_PATH, node.interfaces[0], self._agent_call, None, None)
            self.bus.call_sync(BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                               GLib.Variant("(os)", (AGENT_PATH, "DisplayYesNo")), None, Gio.DBusCallFlags.NONE, 3000, None)
            self.bus.call_sync(BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
                               GLib.Variant("(o)", (AGENT_PATH,)), None, Gio.DBusCallFlags.NONE, 3000, None)
        if self._pair_timer:
            GLib.source_remove(self._pair_timer)
        self._pair_timer = GLib.timeout_add_seconds(seconds, self.end_pairing)
        return True

    def end_pairing(self):
        self._pair_timer = 0
        if self._agent_reg:
            try:
                self.bus.call_sync(BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
                                   GLib.Variant("(o)", (AGENT_PATH,)), None, Gio.DBusCallFlags.NONE, 3000, None)
            except GLib.Error:
                pass
            self.bus.unregister_object(self._agent_reg)
            self._agent_reg = 0
        try:
            self._set(self.adapter, "org.bluez.Adapter1", "Discoverable", GLib.Variant("b", False))
        except GLib.Error:
            pass
        return False

    def _agent_call(self, _c, _s, _p, _iface, method, params, inv):
        args = params.unpack()
        if method in ("RequestConfirmation", "DisplayPasskey"):
            self.emit("passkey", _addr(args[0]), int(args[1]))
            inv.return_value(None)
            if method == "RequestConfirmation":
                GLib.timeout_add(1500, self._after_bond, args[0])
        elif method in ("RequestAuthorization", "AuthorizeService"):
            inv.return_value(None)
        elif method == "RequestPinCode":
            inv.return_value(GLib.Variant("(s)", ("0000",)))
        elif method == "RequestPasskey":
            inv.return_value(GLib.Variant("(u)", (0,)))
        else:
            inv.return_value(None)

    def _after_bond(self, path):
        try:
            if self._prop(path, "org.bluez.Device1", "Paired"):
                self._set(path, "org.bluez.Device1", "Trusted", GLib.Variant("b", True))
                self.emit("paired", _addr(path))
                self.end_pairing()
                return False
        except GLib.Error:
            pass
        return bool(self._agent_reg)  # keep polling while the pairing window is open


def _swallow(bus, res):
    try:
        bus.call_finish(res)
    except GLib.Error:
        pass
