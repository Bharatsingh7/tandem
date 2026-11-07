"""Connection to the phone: TLS with pinned fingerprints, over USB (adb forward) or Wi-Fi (mDNS).

Wire format matches android/.../link/Frames.kt: 4-byte big-endian length + UTF-8 JSON.
Requests  {"t":"req","id":n,"m":method,"a":{...}} -> {"t":"res","id":n,"ok":bool,"r"|"err":...}
Events    {"t":"ev","e":name,"d":data}   (phone -> PC)
"""
import hashlib
import json
import socket
import ssl
import struct
import subprocess
import threading
import time

from gi.repository import GLib, GObject

from . import adb, config
from .config import cfg

PHONE_PORT = 47900
PING_EVERY = 20


def pair_code(phone_fp, pc_fp):
    d = hashlib.sha256((phone_fp + pc_fp).encode()).digest()
    return "%06d" % (struct.unpack(">I", d[:4])[0] % 1_000_000)


def discover(timeout=4):
    """IPv4 addresses advertising _tandem._tcp, via avahi-browse."""
    try:
        out = subprocess.run(["avahi-browse", "-rpt", "_tandem._tcp"], capture_output=True,
                             text=True, timeout=timeout).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    hosts = []
    for line in out.splitlines():
        f = line.split(";")
        if f[0] == "=" and len(f) > 8 and f[2] == "IPv4" and f[7] not in hosts:
            hosts.append(f[7])
    return hosts


def default_gateway():
    """When the PC rides the phone's hotspot, the phone is the gateway."""
    try:
        out = subprocess.run(["ip", "-4", "route", "show", "default"], capture_output=True, text=True).stdout
        return out.split()[2] if out.startswith("default via") else None
    except (OSError, IndexError):
        return None


class Link(GObject.Object):
    __gsignals__ = {
        "state-changed": (GObject.SignalFlags.RUN_FIRST, None, ()),
        "event": (GObject.SignalFlags.RUN_FIRST, None, (str, object)),
    }

    def __init__(self):
        super().__init__()
        config.ensure_identity()
        self.state = "offline"  # offline | connecting | pairing | online | mismatch
        self.transport = None   # "usb" | "wifi"
        self.host = None
        self.serial = None      # adb serial while USB is attached
        self.phone_name = cfg.get("phone_name")
        self.pair_code = None
        self.helper = False
        self._sock = None
        self._wlock = threading.Lock()
        self._pending = {}
        self._next_id = 1
        self._ids = threading.Lock()
        self._wake = threading.Event()
        with open(config.CERT) as f:
            self.my_fp = hashlib.sha256(ssl.PEM_cert_to_DER_cert(f.read())).hexdigest()
        threading.Thread(target=self._loop, name="tandem-link", daemon=True).start()

    # ---- public API (main thread) ----

    @property
    def online(self):
        return self.state == "online"

    def call(self, method, args=None, cb=None):
        """Async request; cb(ok, result_or_error) runs on the GTK main loop."""
        if not self.online:
            if cb:
                GLib.idle_add(cb, False, "Phone not connected")
            return
        with self._ids:
            rid = self._next_id
            self._next_id += 1
        self._pending[rid] = cb
        if not self._send({"t": "req", "id": rid, "m": method, "a": args or {}}):
            self._pending.pop(rid, None)
            if cb:
                GLib.idle_add(cb, False, "Send failed")

    def call_sync(self, method, args=None, timeout=60):
        """Blocking request for worker threads. Raises on error."""
        done = threading.Event()
        box = {}

        def cb(ok, r):
            box["ok"], box["r"] = ok, r
            done.set()

        if not self.online:
            raise ConnectionError("Phone not connected")
        with self._ids:
            rid = self._next_id
            self._next_id += 1
        self._pending[rid] = lambda ok, r: cb(ok, r)
        self._pending[rid].direct = True
        if not self._send({"t": "req", "id": rid, "m": method, "a": args or {}}):
            raise ConnectionError("Send failed")
        if not done.wait(timeout):
            self._pending.pop(rid, None)
            raise TimeoutError(method)
        if not box["ok"]:
            raise RuntimeError(box["r"])
        return box["r"]

    def reconnect(self):
        self.disconnect()
        self._wake.set()

    def disconnect(self):
        s = self._sock
        if s:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def forget(self):
        cfg["phone_fp"] = None
        cfg["phone_name"] = None
        config.save(cfg)
        self.reconnect()

    # ---- connection thread ----

    def _set_state(self, state, **kw):
        def apply():
            self.state = state
            for k, v in kw.items():
                setattr(self, k, v)
            self.emit("state-changed")
        GLib.idle_add(apply)

    def _targets(self):
        serials = adb.devices()
        self.serial = serials[0] if serials else None
        if self.serial and adb.forward(self.serial):
            yield "127.0.0.1", adb.LOCAL_PORT, "usb"
        seen = set()
        for h in discover() + [cfg.get("last_host"), default_gateway()]:
            if h and h not in seen:
                seen.add(h)
                yield h, PHONE_PORT, "wifi"

    def _loop(self):
        while True:
            for host, port, transport in self._targets():
                try:
                    self._session(host, port, transport)
                    break
                except (OSError, ssl.SSLError, ValueError, struct.error):
                    continue
            if self.state != "mismatch":
                self._set_state("offline", transport=None, pair_code=None, helper=False)
            self._wake.wait(4)
            self._wake.clear()

    def _session(self, host, port, transport):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE  # authenticity comes from fingerprint pinning below
        ctx.load_cert_chain(config.CERT, config.KEY)
        raw = socket.create_connection((host, port), timeout=3 if transport == "wifi" else 5)
        s = ctx.wrap_socket(raw)
        fp = hashlib.sha256(s.getpeercert(binary_form=True)).hexdigest()
        pinned = cfg.get("phone_fp")
        if pinned and fp != pinned:
            s.close()
            self._set_state("mismatch")
            raise ValueError("phone fingerprint changed")
        s.settimeout(75)
        self._sock = s
        reader = s.makefile("rb")
        try:
            self._send({"t": "hello", "v": 1, "name": socket.gethostname(), "pair": not pinned})
            hello = self._read(reader)
            name = hello.get("name", "Phone")
            if hello.get("trusted") and pinned:
                self._online(name, transport, host)
            else:
                self._set_state("pairing", phone_name=name, transport=transport, pair_code=pair_code(fp, self.my_fp))
            threading.Thread(target=self._pinger, args=(s,), daemon=True).start()
            while True:
                f = self._read(reader)
                t = f.get("t")
                if t == "res":
                    cb = self._pending.pop(f.get("id"), None)
                    if cb:
                        res = (True, f.get("r")) if f.get("ok") else (False, f.get("err", "error"))
                        if getattr(cb, "direct", False):
                            cb(*res)
                        else:
                            GLib.idle_add(cb, *res)
                elif t == "ev":
                    e, d = f.get("e"), f.get("d")
                    if e == "pair.accepted":
                        cfg["phone_fp"] = fp
                        config.save(cfg)
                        self._online(name, transport, host)
                    elif e == "pair.rejected":
                        self._set_state("offline", pair_code=None)
                    else:
                        GLib.idle_add(self.emit, "event", e, d)
        finally:
            self._sock = None
            for rid, cb in list(self._pending.items()):
                self._pending.pop(rid, None)
                if cb and getattr(cb, "direct", False):
                    cb(False, "disconnected")
                elif cb:
                    GLib.idle_add(cb, False, "disconnected")
            try:
                s.close()
            except OSError:
                pass

    def _online(self, name, transport, host):
        cfg["phone_name"] = name
        if transport == "wifi":
            cfg["last_host"] = host
        config.save(cfg)
        self._set_state("online", phone_name=name, transport=transport, host=host, pair_code=None)
        threading.Thread(target=self._ensure_helper, daemon=True).start()

    def _ensure_helper(self):
        """Start (or replace) the shell helper over adb when USB is attached."""
        time.sleep(0.3)
        try:
            info = self.call_sync("shell.info", timeout=10)
            ok = info.get("running") == info["version"]
            if not ok and self.serial:
                adb.start_helper(self.serial, info["apk"], info["token"], info["version"])
                time.sleep(1.5)
                info = self.call_sync("shell.info", timeout=10)
                ok = info.get("running") == info["version"]
            GLib.idle_add(self._helper_state, ok)
        except Exception:
            pass

    def _helper_state(self, ok):
        self.helper = ok
        self.emit("state-changed")

    def _pinger(self, s):
        while self._sock is s:
            time.sleep(PING_EVERY)
            if self._sock is s and not self._send({"t": "req", "id": 0, "m": "ping", "a": {}}):
                break

    def _send(self, obj):
        s = self._sock
        if not s:
            return False
        b = json.dumps(obj, separators=(",", ":")).encode()
        try:
            with self._wlock:
                s.sendall(struct.pack(">I", len(b)) + b)
            return True
        except OSError:
            self.disconnect()
            return False

    @staticmethod
    def _read(reader):
        head = reader.read(4)
        if len(head) < 4:
            raise ConnectionError("closed")
        (n,) = struct.unpack(">I", head)
        if n > 24 << 20:
            raise ValueError("frame too large")
        body = reader.read(n)
        if len(body) < n:
            raise ConnectionError("closed")
        return json.loads(body)
