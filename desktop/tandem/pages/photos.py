import base64

from gi.repository import Adw, Gdk, Gio, GLib, Gtk

from ..widgets import clear, label, scrolled, texture, when
from .base import Page

PAGE = 60


class PhotosPage(Page):
    title = "Photos & Files"

    def __init__(self, app, win):
        super().__init__(app, win)
        self.items = []
        self.thumbs = {}
        self._loading = False
        refresh = Gtk.Button(icon_name="view-refresh-symbolic", tooltip_text="Refresh")
        refresh.connect("clicked", lambda *_: self.reload())
        self.header.pack_start(refresh)
        send = Gtk.Button(css_classes=["suggested-action"])
        send.set_child(Adw.ButtonContent(icon_name="document-send-symbolic", label="Send files to phone"))
        send.connect("clicked", lambda *_: win.pages["home"]._send_files() if "home" in win.pages else None)
        self.header.pack_end(send)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.progress = Gtk.ProgressBar(visible=False, show_text=True, margin_start=16, margin_end=16, margin_top=8)
        box.append(self.progress)
        hint = label("Drop files here to send them to the phone's Download/Tandem. Share anything on the phone to "
                     "“Send to PC” and it lands in ~/Downloads/Tandem.", css=("caption", "muted"), wrap=True)
        hint.set_margin_start(18)
        hint.set_margin_top(10)
        box.append(hint)
        self.grid = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=10, min_children_per_line=3,
                                row_spacing=8, column_spacing=8, margin_top=12, margin_start=16, margin_end=16, margin_bottom=16,
                                valign=Gtk.Align.START, homogeneous=True)
        self.scroller = scrolled(self.grid)
        self.scroller.get_vadjustment().connect("value-changed", self._maybe_more)
        self.stack = Gtk.Stack()
        self.stack.add_named(self.offline_page("folder-pictures-symbolic"), "offline")
        self.stack.add_named(self.scroller, "grid")
        box.append(self.stack)
        self.set_body(box)

        drop = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
        drop.connect("drop", self._drop)
        self.add_controller(drop)
        app.transfers.connect("progress", self._progress)
        app.link.connect("state-changed", lambda *_: self._link())
        self._link()

    def _link(self):
        if self.app.link.online:
            self.stack.set_visible_child_name("grid")
            if not self.items:
                self.reload()
        else:
            self.stack.set_visible_child_name("offline")

    def shown(self, **_):
        if self.app.link.online and not self.items:
            self.reload()

    def reload(self):
        self.items = []
        clear(self.grid)
        self._more()

    def _maybe_more(self, adj):
        if adj.get_value() + adj.get_page_size() > adj.get_upper() - 400:
            self._more()

    def _more(self):
        if self._loading or not self.app.link.online:
            return
        self._loading = True

        def got(ok, rows):
            self._loading = False
            if not ok:
                return
            for it in rows:
                self.items.append(it)
                self.grid.append(self._tile(it))
        self.app.link.call("photos.list", {"limit": PAGE, "offset": len(self.items)}, got)

    def _tile(self, it):
        pic = Gtk.Picture(content_fit=Gtk.ContentFit.COVER, css_classes=["photo-tile"], can_shrink=True)
        pic.set_size_request(150, 150)
        over = Gtk.Overlay()
        over.set_child(pic)
        if it["video"]:
            over.add_overlay(Gtk.Image(icon_name="media-playback-start-symbolic", pixel_size=28,
                                       halign=Gtk.Align.CENTER, valign=Gtk.Align.CENTER))
        btn = Gtk.Button(css_classes=["flat"], tooltip_text=f"{it['name']} · {when(it['date'])}", child=over)
        btn.connect("clicked", lambda *_: self._download(it))
        if it["id"] in self.thumbs:
            pic.set_paintable(self.thumbs[it["id"]])
        else:
            def got(ok, r):
                if ok and r.get("jpeg"):
                    t = texture(base64.b64decode(r["jpeg"]))
                    self.thumbs[it["id"]] = t
                    pic.set_paintable(t)
            self.app.link.call("photos.thumb", {"id": it["id"], "video": it["video"], "size": 300}, got)
        return btn

    def _download(self, it):
        self.app.toast(f"Copying {it['name']}…")

        def done(path, err):
            self.progress.set_visible(False)
            if err:
                self.app.toast(f"Copy failed: {err}")
                return
            Gio.AppInfo.launch_default_for_uri(path.as_uri(), None)
        self.app.transfers.download_media(it, done)

    def _drop(self, _t, value, _x, _y):
        paths = [f.get_path() for f in value.get_files() if f.get_path()]
        if paths:
            self.app.transfers.send_files(paths, lambda ok, errs: self.app.toast(
                f"Sent {ok} file(s) to phone" + (f" · {errs[0]}" if errs else "")))
        return True

    def _progress(self, _t, name, frac):
        self.progress.set_visible(frac < 1.0)
        self.progress.set_fraction(frac)
        self.progress.set_text(f"{name} · {int(frac * 100)}%")
