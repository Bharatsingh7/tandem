"""File transfer: phone -> ~/Downloads/Tandem (share sheet), PC -> phone Download/Tandem."""
import base64
import mimetypes
import os
import threading
from pathlib import Path

from gi.repository import Gio, GLib, GObject

from .config import cfg

CHUNK = 192 * 1024


def unique_path(folder, name):
    folder.mkdir(parents=True, exist_ok=True)
    p = folder / (os.path.basename(name) or "file")
    stem, suffix, n = p.stem, p.suffix, 1
    while p.exists():
        p = folder / f"{stem} ({n}){suffix}"
        n += 1
    return p


class Transfers(GObject.Object):
    __gsignals__ = {"progress": (GObject.SignalFlags.RUN_FIRST, None, (str, float))}

    def __init__(self, app):
        super().__init__()
        self.app = app
        self.link = app.link
        self._incoming = {}
        self.link.connect("event", self._on_event)

    def _on_event(self, _l, name, d):
        if name == "file.offer":
            p = unique_path(Path(cfg["downloads"]), d["name"])
            self._incoming[d["tid"]] = {"path": p, "f": open(p, "wb"), "size": d.get("size", -1), "got": 0, "name": d["name"]}
        elif name == "file.chunk":
            t = self._incoming.get(d["tid"])
            if t:
                b = base64.b64decode(d["data"])
                t["f"].write(b)
                t["got"] += len(b)
                if t["size"] > 0:
                    self.emit("progress", t["name"], t["got"] / t["size"])
        elif name == "file.done":
            t = self._incoming.pop(d["tid"], None)
            if t:
                t["f"].close()
                self.emit("progress", t["name"], 1.0)
                self._received(t["path"])

    def _received(self, path):
        def act(key):
            if key == "open" or key == "default":
                Gio.AppInfo.launch_default_for_uri(path.as_uri(), None)
            elif key == "folder":
                Gio.AppInfo.launch_default_for_uri(path.parent.as_uri(), None)
        image = str(path) if (mimetypes.guess_type(path.name)[0] or "").startswith("image/") else None
        self.app.notifier.notify(f"Received {path.name}", f"From your phone · saved to {path.parent}",
                                 [("default", "Open"), ("open", "Open"), ("folder", "Show in folder")], act, image=image,
                                 category="transfer.complete")

    def send_files(self, paths, done_cb=None):
        """Upload files to the phone on a worker thread; done_cb(ok_count, errors) on the main loop."""
        def work():
            ok, errors = 0, []
            for path in paths:
                path = Path(path)
                try:
                    size = path.stat().st_size
                    tid = self.link.call_sync("file.begin", {"name": path.name, "size": size,
                                                             "mime": mimetypes.guess_type(path.name)[0] or "application/octet-stream"})["tid"]
                    sent = 0
                    with open(path, "rb") as f:
                        while chunk := f.read(CHUNK):
                            self.link.call_sync("file.chunk", {"tid": tid, "data": base64.b64encode(chunk).decode()})
                            sent += len(chunk)
                            GLib.idle_add(self.emit, "progress", path.name, sent / max(size, 1))
                    self.link.call_sync("file.end", {"tid": tid})
                    ok += 1
                except Exception as e:
                    errors.append(f"{path.name}: {e}")
            if done_cb:
                GLib.idle_add(done_cb, ok, errors)
        threading.Thread(target=work, daemon=True).start()

    def download_media(self, item, done_cb):
        """Copy a phone photo/video to ~/Pictures/Tandem."""
        def work():
            try:
                p = unique_path(Path.home() / "Pictures" / "Tandem", item["name"])
                off = 0
                with open(p, "wb") as f:
                    while True:
                        r = self.link.call_sync("photos.read", {"id": item["id"], "video": item["video"], "offset": off})
                        b = base64.b64decode(r["data"])
                        f.write(b)
                        off += len(b)
                        if item.get("size"):
                            GLib.idle_add(self.emit, "progress", item["name"], off / item["size"])
                        if r["eof"]:
                            break
                GLib.idle_add(done_cb, p, None)
            except Exception as e:
                GLib.idle_add(done_cb, None, str(e))
        threading.Thread(target=work, daemon=True).start()
