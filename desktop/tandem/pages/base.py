from gi.repository import Adw, Gtk


class Page(Adw.Bin):
    """A content page with its own header bar."""
    title = ""

    def __init__(self, app, win):
        super().__init__()
        self.app = app
        self.win = win
        self.view = Adw.ToolbarView()
        self.header = Adw.HeaderBar(show_title=True)
        self.header.set_title_widget(Adw.WindowTitle(title=self.title))
        self.view.add_top_bar(self.header)
        self.set_child(self.view)

    def set_body(self, w):
        self.view.set_content(w)

    def offline_page(self, icon="phone-symbolic"):
        return Adw.StatusPage(icon_name=icon, title="Phone not connected",
                              description="Open Tandem on your phone. It connects over Wi-Fi, or USB with debugging on.")
