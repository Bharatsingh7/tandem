"""Main window: sidebar with the phone card and pages, content stack on the right."""
from gi.repository import Adw, Gio, GLib, Gtk

from .widgets import label

PAGES = [
    ("home", "Home", "phone-symbolic"),
    ("messages", "Messages", "mail-unread-symbolic"),
    ("calls", "Calls", "call-start-symbolic"),
    ("contacts", "Contacts", "x-office-address-book-symbolic"),
    ("photos", "Photos & Files", "folder-pictures-symbolic"),
    ("notifications", "Notifications", "preferences-system-notifications-symbolic"),
    ("settings", "Settings", "emblem-system-symbolic"),
]


class MainWindow(Adw.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title="Tandem", default_width=1200, default_height=780)
        self.app = app
        self.set_size_request(380, 500)
        self.set_hide_on_close(True)
        self.pages = {}
        self.badges = {}

        self.toasts = Adw.ToastOverlay()
        self.split = Adw.NavigationSplitView(min_sidebar_width=230, max_sidebar_width=280)
        self.toasts.set_child(self.split)
        self.set_content(self.toasts)

        bp = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 720sp"))
        bp.add_setter(self.split, "collapsed", True)
        self.add_breakpoint(bp)

        # Sidebar
        side = Adw.ToolbarView()
        hb = Adw.HeaderBar()
        hb.set_title_widget(Adw.WindowTitle(title="Tandem"))
        menu = Gio.Menu()
        menu.append("Quit Tandem", "app.quit")
        hb.pack_end(Gtk.MenuButton(icon_name="open-menu-symbolic", menu_model=menu))
        side.add_top_bar(hb)
        sbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        sbox.append(self._phone_card())
        self.nav = Gtk.ListBox(css_classes=["navigation-sidebar"], selection_mode=Gtk.SelectionMode.SINGLE)
        for key, title, icon in PAGES:
            row = Gtk.ListBoxRow()
            row.key = key
            box = Gtk.Box(spacing=12, margin_start=6, margin_end=6, margin_top=4, margin_bottom=4)
            box.append(Gtk.Image(icon_name=icon))
            box.append(label(title))
            badge = label("", css=("badge",), xalign=0.5)
            badge.set_hexpand(True)
            badge.set_halign(Gtk.Align.END)
            badge.set_visible(False)
            self.badges[key] = badge
            box.append(badge)
            row.set_child(box)
            self.nav.append(row)
        self.nav.connect("row-activated", lambda _l, row: self.show_page(row.key))
        sbox.append(self.nav)
        side.set_content(sbox)
        self.split.set_sidebar(Adw.NavigationPage(title="Tandem", child=side))

        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=120)
        self.content_page = Adw.NavigationPage(title="Home", child=self.stack)
        self.split.set_content(self.content_page)

        app.link.connect("state-changed", lambda *_: self._link_changed())
        app.messages.connect("changed", lambda *_: self._badges())
        app.notifs.connect("changed", lambda *_: self._badges())
        self._link_changed()
        self.show_page("home")

    # ---- phone card ----

    def _phone_card(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, css_classes=["phone-card"])
        top = Gtk.Box(spacing=8)
        self.dot = Gtk.Box(css_classes=["dot"], valign=Gtk.Align.CENTER)
        top.append(self.dot)
        self.card_name = label("No phone", css=("name",), ellipsize=True)
        self.card_name.set_hexpand(True)
        top.append(self.card_name)
        box.append(top)
        self.card_sub = label("", css=("muted", "caption"), ellipsize=True)
        box.append(self.card_sub)
        return box

    def _link_changed(self):
        link = self.app.link
        self.dot.remove_css_class("on")
        self.dot.remove_css_class("wait")
        self.card_name.set_text(link.phone_name or "No phone yet")
        if link.online:
            self.dot.add_css_class("on")
            via = "USB" if link.transport == "usb" else "Wi-Fi"
            self.card_sub.set_text(f"Connected over {via}")
        elif link.state == "pairing":
            self.dot.add_css_class("wait")
            self.card_sub.set_text("Waiting for you to accept on the phone")
        elif link.state == "mismatch":
            self.card_sub.set_text("Phone identity changed. Re-pair in Settings")
        else:
            self.card_sub.set_text("Searching… open Tandem on the phone")
        self.status_changed()

    def status_changed(self):
        s = self.app.status
        if self.app.link.online and s.get("battery"):
            b = s["battery"]
            net = s.get("network", {})
            bits = [f"{b['level']}%" + (" charging" if b.get("charging") else ""), net.get("carrier", ""), net.get("type", "")]
            via = "USB" if self.app.link.transport == "usb" else "Wi-Fi"
            self.card_sub.set_text(" · ".join(x for x in bits if x) + f" · {via}")
        for p in self.pages.values():
            if hasattr(p, "status_changed"):
                p.status_changed()

    def _badges(self):
        unread = sum(r["unread"] or 0 for r in self.app.messages.chats()) + sum(t["unread"] for t in self.app.messages.threads)
        self._badge("messages", unread)
        n = sum(1 for x in self.app.notifs.items.values() if x.get("clearable"))
        self._badge("notifications", n)

    def _badge(self, key, n):
        b = self.badges[key]
        b.set_text(str(n) if n < 100 else "99+")
        b.set_visible(n > 0)

    # ---- pages ----

    def _page(self, key):
        if key not in self.pages:
            from . import pages
            cls = {"home": pages.HomePage, "messages": pages.MessagesPage, "calls": pages.CallsPage,
                   "contacts": pages.ContactsPage, "photos": pages.PhotosPage,
                   "notifications": pages.NotificationsPage, "settings": pages.SettingsPage}[key]
            page = cls(self.app, self)
            self.pages[key] = page
            self.stack.add_named(page, key)
        return self.pages[key]

    def show_page(self, key, **kw):
        page = self._page(key)
        self.stack.set_visible_child_name(key)
        title = next(t for k, t, _ in PAGES if k == key)
        self.content_page.set_title(title)
        for row in self._rows():
            if row.key == key:
                self.nav.select_row(row)
        self.split.set_show_content(True)
        if hasattr(page, "shown"):
            page.shown(**kw)

    def current_page(self):
        return self.stack.get_visible_child_name()

    def _rows(self):
        r = self.nav.get_first_child()
        while r:
            yield r
            r = r.get_next_sibling()

    def toast(self, text):
        self.toasts.add_toast(Adw.Toast(title=GLib.markup_escape_text(text), timeout=4))
