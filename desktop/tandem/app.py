"""Tandem desktop: one long-running instance (tray-less, started at login with --background)."""
import sys
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, Gio, GLib, Gtk  # noqa: E402

from . import APP_ID  # noqa: E402
from .bt import Bluetooth  # noqa: E402
from .calls import CallManager  # noqa: E402
from .clipboard import ClipboardSync  # noqa: E402
from .config import cfg  # noqa: E402
from .contacts import Contacts  # noqa: E402
from .link import Link  # noqa: E402
from .messages import MessageCenter  # noqa: E402
from .mpris import Mpris  # noqa: E402
from .notifier import Notifier  # noqa: E402
from .notifs import NotificationCenter  # noqa: E402
from .store import Store  # noqa: E402
from .transfers import Transfers  # noqa: E402

TOGGLES = ["bluetooth", "wifi", "hotspot", "data", "airplane", "dnd", "location", "torch"]


class TandemApp(Adw.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)
        self.window = None
        self.status = {}
        opts = [
            ("background", "Start without opening the window"),
            ("quit", "Quit the running instance"),
            ("ring", "Ring the phone (find my phone)"),
            ("lock", "Lock the phone"),
            ("clipboard", "Send the PC clipboard to the phone"),
        ]
        for name, desc in opts:
            self.add_main_option(name, 0, GLib.OptionFlags.NONE, GLib.OptionArg.NONE, desc, None)
        self.add_main_option("dial", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING, "Open the dialer (optionally with a number)", "NUMBER")
        self.add_main_option("call", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING, "Call a number right away", "NUMBER")
        self.add_main_option("sms", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING, "Write an SMS", "NUMBER")
        self.add_main_option("toggle", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING,
                             "Set a phone toggle, e.g. hotspot=on (" + ", ".join(TOGGLES) + ")", "NAME=on|off")
        self.add_main_option("send", 0, GLib.OptionFlags.NONE, GLib.OptionArg.FILENAME_ARRAY, "Send files to the phone", "FILE")
        self.add_main_option("page", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING,
                             "Open a page: home, messages, calls, contacts, photos, notifications, settings", "PAGE")
        self.add_main_option("debug-shot", 0, GLib.OptionFlags.HIDDEN, GLib.OptionArg.STRING,
                             "Render a page of the window to a PNG (development aid)", "PAGE:PATH")
        self.add_main_option("open-url", 0, GLib.OptionFlags.NONE, GLib.OptionArg.STRING, "Open a link on the phone", "URL")

    def do_startup(self):
        Adw.Application.do_startup(self)
        css = Gtk.CssProvider()
        css.load_from_path(str(Path(__file__).with_name("style.css")))
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        self.store = Store()
        self.notifier = Notifier()
        self.link = Link()
        self.bt = Bluetooth()
        self.contacts = Contacts(self.store, self.link)
        self.notifs = NotificationCenter(self)
        self.messages = MessageCenter(self)
        self.calls = CallManager(self)
        self.transfers = Transfers(self)
        self.mpris = Mpris(self.link, lambda: self.present("home"))
        self.clipboard = ClipboardSync(self.link)

        self.link.connect("event", self._on_event)
        self.link.connect("state-changed", self._on_state)

        quit_action = Gio.SimpleAction.new("quit", None)
        quit_action.connect("activate", lambda *_: self.quit())
        self.add_action(quit_action)
        self.set_accels_for_action("app.quit", ["<Ctrl>q"])
        self.hold()  # keep running with the window closed

    def _on_state(self, link):
        if link.online:
            link.call("status.get", cb=lambda ok, r: ok and self._status(r))
        if link.state == "pairing":
            self.present("home")

    def _on_event(self, _l, name, data):
        if name == "status":
            self._status(data)

    def _status(self, s):
        self.status = s
        self.messages.sms_pkg = s.get("smsApp") or None
        if self.window:
            self.window.status_changed()

    # ---- window ----

    def present(self, page=None, **kw):
        if not self.window:
            from .window import MainWindow
            self.window = MainWindow(self)
        if page:
            self.window.show_page(page, **kw)
        self.window.present()

    def window_focused_on(self, page):
        return bool(self.window and self.window.is_active() and self.window.current_page() == page)

    def toast(self, text):
        if self.window and self.window.get_visible():
            self.window.toast(text)
        else:
            self.notifier.notify("Tandem", text, timeout=4000)

    # ---- command line ----

    def do_command_line(self, cmd):
        opts = cmd.get_options_dict().end().unpack()
        args = [a for a in cmd.get_arguments()[1:]]
        if opts.get("quit"):
            self.quit()
            return 0
        acted = False
        for a in args:
            if a.startswith("tel:"):
                self.present("calls", number=_uri_number(a))
                acted = True
            elif a.startswith("sms:") or a.startswith("smsto:"):
                self.present("messages", sms=_uri_number(a))
                acted = True
        if "dial" in opts:
            self.present("calls", number=opts["dial"])
            acted = True
        if "call" in opts:
            self.calls.dial(opts["call"])
            acted = True
        if "sms" in opts:
            self.present("messages", sms=opts["sms"])
            acted = True
        if "toggle" in opts:
            name, _, val = opts["toggle"].partition("=")
            self.set_toggle(name.strip(), val.strip().lower() in ("1", "on", "true", "yes"))
            acted = True
        if opts.get("ring"):
            self.link.call("phone.ring", {"on": True})
            acted = True
        if opts.get("lock"):
            self.link.call("phone.lock")
            acted = True
        if opts.get("clipboard"):
            self.clipboard.send_now()
            acted = True
        if "send" in opts:
            files = [f.decode() if isinstance(f, bytes) else f for f in opts["send"]]
            cwd = cmd.get_cwd() or "."
            self.transfers.send_files([str(Path(cwd, f)) for f in files], lambda ok, errs: self.toast(
                f"Sent {ok} file(s) to phone" + (f" · {len(errs)} failed" if errs else "")))
            acted = True
        if "page" in opts:
            self.present(opts["page"])
            acted = True
        if "debug-shot" in opts:
            page, _, path = opts["debug-shot"].partition(":")
            self._debug_shot(page, path)
            acted = True
        if "open-url" in opts:
            self.link.call("phone.openUrl", {"url": opts["open-url"]}, lambda ok, r: ok or self.toast(str(r)))
            acted = True
        if not acted and not opts.get("background"):
            self.present()
        return 0

    def _debug_shot(self, page, path):
        """Renders the window itself, so nothing else on screen is captured."""
        if not self.window:
            self.present()
        self.window.show_page(page)

        def shot():
            w = self.window
            snap = Gtk.Snapshot()
            Gtk.WidgetPaintable.new(w).snapshot(snap, w.get_width(), w.get_height())
            w.get_native().get_renderer().render_texture(snap.to_node(), None).save_to_png(path)
            return False
        GLib.timeout_add(1200, shot)

    def set_toggle(self, name, on, cb=None):
        def done(ok, r):
            if ok:
                self._status(r)
            else:
                self.toast(f"Couldn't switch {name}: {r}")
            if cb:
                cb(ok, r)
        self.link.call("toggle.set", {"name": name, "on": on}, done)


def _uri_number(uri):
    return GLib.uri_unescape_string(uri.split(":", 1)[1].split("?")[0], None) or ""


def main(argv=None):
    Adw.init()
    return TandemApp().run(argv or sys.argv)
