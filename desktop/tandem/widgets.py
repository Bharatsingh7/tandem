"""Small shared UI helpers."""
import time

from gi.repository import Adw, Gdk, GLib, Gtk


def texture(data: bytes):
    try:
        return Gdk.Texture.new_from_bytes(GLib.Bytes.new(data))
    except GLib.Error:
        return None


def avatar(name, size=40, image: bytes = None):
    a = Adw.Avatar(size=size, text=name or "?", show_initials=True)
    if image:
        t = texture(image)
        if t:
            a.set_custom_image(t)
    return a


def label(text="", css=(), xalign=0.0, wrap=False, ellipsize=False, lines=0, selectable=False):
    l = Gtk.Label(label=text, xalign=xalign)
    for c in css:
        l.add_css_class(c)
    if wrap:
        l.set_wrap(True)
        l.set_wrap_mode(2)  # WORD_CHAR
        l.set_natural_wrap_mode(Gtk.NaturalWrapMode.WORD)
    if ellipsize:
        l.set_ellipsize(3)  # END
    if lines:
        l.set_lines(lines)
    l.set_selectable(selectable)
    return l


def when(ms):
    """Short, human timestamp: 14:05, Yesterday, Mon, 12 Oct, 12 Oct 2024."""
    if not ms:
        return ""
    t = GLib.DateTime.new_from_unix_local(ms / 1000)
    now = GLib.DateTime.new_now_local()
    days = (now.get_ymd()[0] - t.get_ymd()[0]) * 366 + now.get_day_of_year() - t.get_day_of_year()
    if days == 0:
        return t.format("%H:%M")
    if days == 1:
        return "Yesterday"
    if days < 7:
        return t.format("%a")
    if t.get_year() == now.get_year():
        return t.format("%-d %b")
    return t.format("%-d %b %Y")


def day_label(ms):
    t = GLib.DateTime.new_from_unix_local(ms / 1000)
    w = when(ms)
    return "Today" if ":" in w else w if w in ("Yesterday",) else t.format("%A, %-d %B")


def duration(sec):
    sec = int(sec)
    if sec < 60:
        return f"{sec}s"
    m, s = divmod(sec, 60)
    if m < 60:
        return f"{m}m {s:02d}s"
    h, m = divmod(m, 60)
    return f"{h}h {m:02d}m"


def clamp(child, width=760):
    c = Adw.Clamp(maximum_size=width, tightening_threshold=int(width * 0.8))
    c.set_child(child)
    return c


def scrolled(child, hscroll=False):
    s = Gtk.ScrolledWindow(vexpand=True, hexpand=True)
    s.set_policy(Gtk.PolicyType.AUTOMATIC if hscroll else Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
    s.set_child(child)
    return s


def clear(box):
    while (c := box.get_first_child()) is not None:
        box.remove(c)


def icon_button(icon, tooltip, cb, css=()):
    b = Gtk.Button(icon_name=icon, tooltip_text=tooltip, valign=Gtk.Align.CENTER)
    for c in css:
        b.add_css_class(c)
    b.connect("clicked", lambda *_: cb())
    return b


def now_ms():
    return int(time.time() * 1000)
