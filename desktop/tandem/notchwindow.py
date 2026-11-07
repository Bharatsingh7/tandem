"""Dynamic Island / notch-style call pill: a small layer-shell overlay at the top-center of the screen.

Ringing → expanded pill with caller name, avatar, pulsing animation, Answer and Decline buttons.
Active → compact pill with caller name, elapsed time, click to expand to the full CallWindow.
"""
import gi

gi.require_version("Gtk4LayerShell", "1.0")
from gi.repository import Adw, Gdk, GLib, Gtk, Gtk4LayerShell as LayerShell

from .widgets import avatar, clear, label


class NotchWindow(Gtk.Window):
    """Pill-shaped overlay anchored to the top-center of the primary monitor."""

    # Geometry
    PILL_HEIGHT = 52
    PILL_WIDTH_COMPACT = 280
    PILL_WIDTH_RINGING = 380
    PILL_WIDTH_ACTIVE_EXPANDED = 380
    TOP_MARGIN = 8

    def __init__(self, app):
        super().__init__(css_classes=["notch-window"])
        self.app = app
        self.cm = app.calls
        self._timer = 0
        self._close_timer = 0
        self._last_state = None
        self._call = None
        self._expanded = False  # True when user hovers/clicks to see more controls

        # Layer-shell setup: overlay layer, anchored to top, no exclusive zone, no keyboard grab.
        LayerShell.init_for_window(self)
        LayerShell.set_layer(self, LayerShell.Layer.OVERLAY)
        LayerShell.set_anchor(self, LayerShell.Edge.TOP, True)
        LayerShell.set_margin(self, LayerShell.Edge.TOP, self.TOP_MARGIN)
        LayerShell.set_exclusive_zone(self, 0)
        LayerShell.set_keyboard_mode(self, LayerShell.KeyboardMode.NONE)
        LayerShell.set_namespace(self, "tandem-call-notch")

        self.set_resizable(False)
        self.set_decorated(False)

        # Outer container — the pill shape.
        self.pill = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, css_classes=["notch-pill"])
        self.set_child(self.pill)

        # Click gesture on the whole pill — expands to full CallWindow during an active call.
        click = Gtk.GestureClick()
        click.connect("released", self._on_pill_click)
        self.pill.add_controller(click)

    # ---- public API called by CallManager ----

    def primary(self):
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

        c = self.primary()
        if not c:
            return
        self._call = c

        title, sub, cid = self.cm.display(c)
        photo = self.app.contacts.photo(cid) if cid else None
        state = c["state"]

        clear(self.pill)

        if state == "ringing" and c.get("incoming"):
            self._build_ringing(c, title, sub, photo)
        else:
            self._build_active(c, title, sub, photo)

        self._last_state = state

        # Start the duration timer for active calls.
        if state == "active" and not self._timer:
            self._timer = GLib.timeout_add(500, self._tick)

        self.set_visible(True)

    def ended(self):
        """Call ended — show a brief "Call ended" then hide."""
        if self._timer:
            GLib.source_remove(self._timer)
            self._timer = 0

        clear(self.pill)
        self.pill.remove_css_class("notch-ringing")
        self.pill.add_css_class("notch-compact")

        row = Gtk.Box(spacing=10, halign=Gtk.Align.CENTER, valign=Gtk.Align.CENTER,
                      margin_start=20, margin_end=20, margin_top=8, margin_bottom=8)
        row.append(Gtk.Image(icon_name="call-stop-symbolic", pixel_size=16, css_classes=["notch-icon-ended"]))
        row.append(label("Call ended", css=("notch-text",)))
        self.pill.append(row)
        self._call = None
        self._last_state = None

        if not self._close_timer:
            self._close_timer = GLib.timeout_add(2000, self._auto_close)

    # ---- ringing layout ----

    def _build_ringing(self, c, title, sub, photo):
        self.pill.remove_css_class("notch-compact")
        self.pill.add_css_class("notch-ringing")

        # Main row: avatar | text | buttons
        row = Gtk.Box(spacing=12, margin_start=12, margin_end=10, margin_top=8, margin_bottom=8)

        # Pulsing ring indicator + avatar
        av_box = Gtk.Box(css_classes=["notch-pulse-ring"])
        av = avatar(title, 40, photo)
        av_box.append(av)
        row.append(av_box)

        # Name and subtitle
        text_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER, hexpand=True)
        text_box.append(label(title, css=("notch-caller",), ellipsize=True))
        text_box.append(label("Incoming call" + (f" · {sub}" if sub else ""),
                              css=("notch-sub",), ellipsize=True))
        row.append(text_box)

        # Decline button
        decline = Gtk.Button(icon_name="call-stop-symbolic",
                             css_classes=["notch-btn", "notch-decline"],
                             tooltip_text="Decline", valign=Gtk.Align.CENTER)
        decline.connect("clicked", lambda *_: self.cm.reject(c["id"]))
        row.append(decline)

        # Answer button
        answer = Gtk.Button(icon_name="call-start-symbolic",
                            css_classes=["notch-btn", "notch-answer"],
                            tooltip_text="Answer", valign=Gtk.Align.CENTER)
        answer.connect("clicked", lambda *_: self.cm.answer(c["id"]))
        row.append(answer)

        self.pill.append(row)

    # ---- active / dialing / holding layout ----

    def _build_active(self, c, title, sub, photo):
        self.pill.remove_css_class("notch-ringing")
        self.pill.add_css_class("notch-compact")

        row = Gtk.Box(spacing=10, margin_start=12, margin_end=10, margin_top=6, margin_bottom=6)

        # Small green dot indicating active call
        dot = Gtk.Box(css_classes=["notch-active-dot"], valign=Gtk.Align.CENTER)
        row.append(dot)

        # Avatar
        av = avatar(title, 32, photo)
        av.set_valign(Gtk.Align.CENTER)
        row.append(av)

        # Name
        name = label(title, css=("notch-caller-sm",), ellipsize=True)
        name.set_hexpand(True)
        name.set_valign(Gtk.Align.CENTER)
        row.append(name)

        # Timer / status text
        state_text = {"dialing": "Calling…", "holding": "On hold", "active": ""}.get(c["state"], "")
        self._timer_label = label(state_text, css=("notch-timer",))
        self._timer_label.set_valign(Gtk.Align.CENTER)
        row.append(self._timer_label)
        self._tick()

        # End call button (small)
        end = Gtk.Button(icon_name="call-stop-symbolic",
                         css_classes=["notch-btn-sm", "notch-decline"],
                         tooltip_text="End call", valign=Gtk.Align.CENTER)
        end.connect("clicked", lambda *_: self.cm.end(c["id"]))
        row.append(end)

        self.pill.append(row)

    # ---- timer ----

    def _tick(self):
        c = self._call
        if not c or c["state"] != "active" or not c.get("since"):
            if not c or c["state"] != "active":
                self._timer = 0
            return bool(c and c["state"] == "active")
        secs = max(0, int(GLib.get_real_time() / 1e6 - c["since"] / 1000))
        h, rem = divmod(secs, 3600)
        m, s = divmod(rem, 60)
        text = f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"
        if hasattr(self, "_timer_label"):
            self._timer_label.set_text(text)
        return True

    # ---- interactions ----

    def _on_pill_click(self, gesture, n_press, x, y):
        # Clicking the pill during an active (non-ringing) call opens the full CallWindow.
        c = self._call
        if not c:
            return
        if c["state"] == "ringing" and c.get("incoming"):
            return  # ringing has its own buttons; don't open full window
        self._show_full_window()

    def _show_full_window(self):
        """Open the full CallWindow and keep the notch visible behind it."""
        win = self.cm._window()
        win.update()
        win.present()

    def _auto_close(self):
        self._close_timer = 0
        if not self.cm.calls:
            self.set_visible(False)
        return False
