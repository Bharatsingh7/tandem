import json

import gi

gi.require_version("GdkPixbuf", "2.0")
from gi.repository import Adw, GdkPixbuf, Gio, GLib, Gtk  # noqa: E402

from ..contacts import EMAIL_TYPES, PHONE_TYPES, digits, pretty  # noqa: E402
from ..widgets import avatar, clear, label, scrolled  # noqa: E402
from .base import Page  # noqa: E402


class ContactsPage(Page):
    title = "Contacts"

    def __init__(self, app, win):
        super().__init__(app, win)
        self.selected = None
        add = Gtk.Button(icon_name="list-add-symbolic", tooltip_text="New contact")
        add.connect("clicked", lambda *_: edit_contact(app, win))
        self.header.pack_start(add)
        menu = Gtk.MenuButton(icon_name="view-more-symbolic")
        pop = Gtk.Popover()
        pbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, margin_top=4, margin_bottom=4)
        for text, cb in (("Sync now", lambda: app.contacts.sync()), ("Import vCard…", self._import),
                         ("Export vCard…", self._export)):
            b = Gtk.Button(label=text, css_classes=["flat"])
            b.get_child().set_xalign(0)
            b.connect("clicked", lambda _b, f=cb: (pop.popdown(), f()))
            pbox.append(b)
        pop.set_child(pbox)
        menu.set_popover(pop)
        self.header.pack_end(menu)

        paned = Gtk.Paned(position=340, shrink_start_child=False)
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.search = Gtk.SearchEntry(placeholder_text="Search name, number, email", margin_top=10, margin_start=10, margin_end=10, margin_bottom=6)
        self.search.connect("search-changed", lambda *_: self._fill())
        left.append(self.search)
        self.count = label("", css=("caption", "muted"), xalign=0)
        self.count.set_margin_start(14)
        left.append(self.count)
        self.list = Gtk.ListBox(css_classes=["navigation-sidebar"])
        self.list.set_header_func(self._letter_header)
        self.list.connect("row-activated", lambda _l, row: self._show(row.cid))
        left.append(scrolled(self.list))
        paned.set_start_child(left)

        self.detail = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14, margin_top=28, margin_start=32, margin_end=32, margin_bottom=28)
        self.detail_stack = Gtk.Stack()
        self.detail_stack.add_named(Adw.StatusPage(icon_name="x-office-address-book-symbolic", title="Your contacts",
                                                   description="Synced both ways with the phone, and searchable offline."), "empty")
        self.detail_stack.add_named(scrolled(Adw.Clamp(maximum_size=640, child=self.detail)), "detail")
        paned.set_end_child(self.detail_stack)
        self.set_body(paned)
        app.contacts.connect("changed", lambda *_: self._refresh())
        self._fill()

    def _refresh(self):
        self._fill()
        if self.selected:
            if self._exists(self.selected):
                self._show(self.selected)
            else:
                self.selected = None
                self.detail_stack.set_visible_child_name("empty")

    def _exists(self, cid):
        return bool(self.app.store.q("SELECT 1 FROM contacts WHERE id=?", (cid,)))

    def _fill(self):
        clear(self.list)
        rows = self.app.contacts.search(self.search.get_text(), 5000)
        pending = self.app.contacts.pending_count()
        self.count.set_text(f"{len(rows)} contacts" + (f" · {pending} edits waiting for the phone" if pending else ""))
        for c in rows:
            row = Gtk.ListBoxRow()
            row.cid = c["id"]
            row.letter = "★" if c["starred"] else (c["name"][:1].upper() if c["name"] and c["name"][0].isalpha() else "#")
            box = Gtk.Box(spacing=10, margin_top=4, margin_bottom=4, margin_start=4)
            box.append(avatar(c["name"], 34, self.app.contacts.photo(c["id"]) if c["has_photo"] else None))
            v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER)
            v.append(label(c["name"], ellipsize=True))
            phones = json.loads(c["phones"])
            if phones:
                v.append(label(pretty(phones[0]["n"]), css=("caption", "muted"), ellipsize=True))
            box.append(v)
            row.set_child(box)
            self.list.append(row)

    @staticmethod
    def _letter_header(row, before):
        if before is None or before.letter != row.letter:
            h = label(row.letter if row.letter != "★" else "FAVOURITES", css=("eyebrow",))
            h.set_margin_top(10)
            h.set_margin_start(10)
            row.set_header(h)
        else:
            row.set_header(None)

    def _show(self, cid):
        c = self.app.contacts.get(cid)
        if not c:
            return
        self.selected = cid
        clear(self.detail)
        self.detail_stack.set_visible_child_name("detail")
        top = Gtk.Box(spacing=20)
        top.append(avatar(c["name"], 96, self.app.contacts.photo(cid)))
        v = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER, spacing=4, hexpand=True)
        v.append(label(c["name"], css=("display",), wrap=True))
        sub = " · ".join(x for x in (c["title"], c["org"]) if x)
        if sub:
            v.append(label(sub, css=("muted",)))
        top.append(v)
        tools = Gtk.Box(spacing=6, valign=Gtk.Align.START)
        star = Gtk.Button(icon_name="starred-symbolic" if c["starred"] else "non-starred-symbolic", css_classes=["flat", "circular"],
                          tooltip_text="Favourite")
        star.connect("clicked", lambda *_: self.app.contacts.star(cid, not c["starred"]))
        tools.append(star)
        edit = Gtk.Button(icon_name="document-edit-symbolic", css_classes=["flat", "circular"], tooltip_text="Edit")
        edit.connect("clicked", lambda *_: edit_contact(self.app, self.win, c))
        tools.append(edit)
        delete = Gtk.Button(icon_name="user-trash-symbolic", css_classes=["flat", "circular"], tooltip_text="Delete")
        delete.connect("clicked", lambda *_: self._delete(c))
        tools.append(delete)
        top.append(tools)
        self.detail.append(top)

        phones = json.loads(c["phones"])
        if phones:
            g = Adw.PreferencesGroup(title="Phone")
            for p in phones:
                row = Adw.ActionRow(title=pretty(p["n"]), subtitle=p["type"].capitalize(), title_selectable=True)
                for icon, tip, cb in (("call-start-symbolic", "Call", lambda n=p["n"]: self.app.calls.dial(n)),
                                      ("mail-unread-symbolic", "SMS", lambda n=p["n"]: self.app.present("messages", sms=n)),
                                      ("emoji-people-symbolic", "WhatsApp chat on phone",
                                       lambda n=p["n"]: self.app.link.call("phone.openUrl", {"url": f"https://wa.me/{_wa(n)}"}))):
                    b = Gtk.Button(icon_name=icon, tooltip_text=tip, css_classes=["flat", "circular"], valign=Gtk.Align.CENTER)
                    b.connect("clicked", lambda _b, f=cb: f())
                    row.add_suffix(b)
                g.add(row)
            self.detail.append(g)
        emails = json.loads(c["emails"])
        if emails:
            g = Adw.PreferencesGroup(title="Email")
            for e in emails:
                row = Adw.ActionRow(title=GLib.markup_escape_text(e["a"]), subtitle=e["type"].capitalize(), title_selectable=True)
                b = Gtk.Button(icon_name="mail-send-symbolic", css_classes=["flat", "circular"], valign=Gtk.Align.CENTER)
                b.connect("clicked", lambda _b, a=e["a"]: Gio.AppInfo.launch_default_for_uri(f"mailto:{a}", None))
                row.add_suffix(b)
                g.add(row)
            self.detail.append(g)
        calls = self.app.store.q("SELECT * FROM calllog ORDER BY date DESC LIMIT 400")
        keys = {digits(p["n"])[-10:] for p in phones}
        mine = [r for r in calls if digits(r["number"])[-10:] in keys][:6]
        if mine:
            from ..widgets import duration, when
            g = Adw.PreferencesGroup(title="Recent calls")
            for r in mine:
                g.add(Adw.ActionRow(title=f"{r['type'].capitalize()} · {when(r['date'])}",
                                    subtitle=duration(r["duration"]) if r["duration"] else pretty(r["number"])))
            self.detail.append(g)

    def _delete(self, c):
        d = Adw.AlertDialog(heading=f"Delete {c['name']}?", body="It's removed from the phone too.")
        d.add_response("cancel", "Cancel")
        d.add_response("delete", "Delete")
        d.set_response_appearance("delete", Adw.ResponseAppearance.DESTRUCTIVE)
        d.connect("response", lambda _d, r: r == "delete" and self.app.contacts.delete(c["id"]))
        d.present(self.win)

    def _import(self):
        f = Gtk.FileDialog(title="Import vCard")
        flt = Gtk.FileFilter(name="vCard")
        flt.add_suffix("vcf")
        store = Gio.ListStore.new(Gtk.FileFilter)
        store.append(flt)
        f.set_filters(store)

        def done(dlg, res):
            try:
                path = dlg.open_finish(res).get_path()
            except GLib.Error:
                return
            n = self.app.contacts.import_vcf(path)
            self.app.toast(f"Adding {n} contacts to the phone…")
        f.open(self.win, None, done)

    def _export(self):
        f = Gtk.FileDialog(title="Export contacts", initial_name="contacts.vcf")

        def done(dlg, res):
            try:
                path = dlg.save_finish(res).get_path()
            except GLib.Error:
                return
            self.app.toast(f"Exported {self.app.contacts.export_vcf(path)} contacts")
        f.save(self.win, None, done)


def _wa(number):
    d = digits(number)
    return "91" + d if len(d) == 10 else d.lstrip("0")


def edit_contact(app, win, contact=None, number=None):
    """Add/edit dialog. Saves through the phone (queued if it's offline)."""
    d = Adw.Dialog(title="Edit contact" if contact else "New contact", content_width=460)
    view = Adw.ToolbarView()
    hb = Adw.HeaderBar(show_end_title_buttons=False, show_start_title_buttons=False)
    cancel = Gtk.Button(label="Cancel")
    cancel.connect("clicked", lambda *_: d.close())
    hb.pack_start(cancel)
    save = Gtk.Button(label="Save", css_classes=["suggested-action"])
    hb.pack_end(save)
    view.add_top_bar(hb)
    page = Adw.PreferencesPage()
    state = {"photo": None}

    head = Adw.PreferencesGroup()
    av_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, halign=Gtk.Align.CENTER, margin_bottom=8)
    av = avatar(contact["name"] if contact else "", 88, app.contacts.photo(contact["id"]) if contact else None)
    av_box.append(av)
    pick = Gtk.Button(label="Choose photo…", css_classes=["flat"])
    av_box.append(pick)
    head.add(av_box)
    page.add(head)

    names = Adw.PreferencesGroup()
    given = Adw.EntryRow(title="First name", text=contact["given"] if contact else "")
    family = Adw.EntryRow(title="Last name", text=contact["family"] if contact else "")
    if contact and not contact["given"] and not contact["family"]:
        given.set_text(contact["name"])
    org = Adw.EntryRow(title="Company", text=contact["org"] if contact else "")
    title = Adw.EntryRow(title="Job title", text=contact["title"] if contact else "")
    for w in (given, family, org, title):
        names.add(w)
    page.add(names)

    def list_group(heading, items, types, key, placeholder):
        g = Adw.PreferencesGroup(title=heading)
        rows = []

        def add_row(value="", typ=types[0]):
            r = Adw.EntryRow(title=placeholder, text=value)
            dd = Gtk.DropDown(model=Gtk.StringList.new([t.capitalize() for t in types]), valign=Gtk.Align.CENTER,
                              css_classes=["flat"])
            dd.set_selected(types.index(typ) if typ in types else len(types) - 1)
            r.add_suffix(dd)
            rm = Gtk.Button(icon_name="edit-delete-symbolic", css_classes=["flat", "circular"], valign=Gtk.Align.CENTER)
            r.add_suffix(rm)
            r.dd = dd
            rows.append(r)
            g.add(r)
            rm.connect("clicked", lambda *_: (g.remove(r), rows.remove(r)))

        for it in items:
            add_row(it[key], it["type"])
        more = Gtk.Button(icon_name="list-add-symbolic", css_classes=["flat"], tooltip_text=f"Add {heading.lower()}")
        more.connect("clicked", lambda *_: add_row())
        g.set_header_suffix(more)
        page.add(g)
        return lambda: [{key: r.get_text().strip(), "type": types[r.dd.get_selected()]} for r in rows if r.get_text().strip()]

    phones = json.loads(contact["phones"]) if contact else ([{"n": number, "type": "mobile"}] if number else [{"n": "", "type": "mobile"}])
    get_phones = list_group("Phone", phones, PHONE_TYPES, "n", "Number")
    get_emails = list_group("Email", json.loads(contact["emails"]) if contact else [], EMAIL_TYPES, "a", "Address")

    def choose_photo(*_):
        f = Gtk.FileDialog(title="Contact photo")

        def done(dlg, res):
            try:
                path = dlg.open_finish(res).get_path()
                pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(path, 512, 512, True)
                side = min(pb.get_width(), pb.get_height())
                pb = pb.new_subpixbuf((pb.get_width() - side) // 2, (pb.get_height() - side) // 2, side, side)
                ok, buf = pb.save_to_bufferv("jpeg", ["quality"], ["88"])
                state["photo"] = bytes(buf)
                from ..widgets import texture
                av.set_custom_image(texture(state["photo"]))
            except GLib.Error:
                pass
        f.open(win, None, done)

    pick.connect("clicked", choose_photo)

    def do_save(*_):
        data = {"given": given.get_text().strip(), "family": family.get_text().strip(), "org": org.get_text().strip(),
                "title": title.get_text().strip(), "phones": get_phones(), "emails": get_emails()}
        data["name"] = f"{data['given']} {data['family']}".strip()
        if not data["name"]:
            app.toast("Give the contact a name")
            return
        if contact:
            data["id"] = contact["id"]
            data["starred"] = bool(contact["starred"])
        if state["photo"]:
            import base64
            data["jpeg"] = base64.b64encode(state["photo"]).decode()

        def done(ok, r):
            if ok:
                app.toast("Saved to phone" if app.link.online else "Saved. Will sync when the phone connects")
            else:
                app.toast(f"Couldn't save: {r}")
        app.contacts.save(data, done)
        d.close()

    save.connect("clicked", do_save)
    view.set_content(page)
    d.set_child(view)
    d.present(win)
