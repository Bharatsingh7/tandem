"""Two-way clipboard sync on Wayland through wl-clipboard (GTK only sees the clipboard while focused)."""
import subprocess
import threading

from gi.repository import GLib

from .config import cfg


class ClipboardSync:
    def __init__(self, link):
        self.link = link
        self._last = None
        self._proc = None
        link.connect("event", self._on_event)
        threading.Thread(target=self._watch, daemon=True).start()

    def _on_event(self, _l, name, data):
        if name == "clip" and cfg.get("clipboard_sync") and data.get("text"):
            self.set_local(data["text"])

    def set_local(self, text):
        self._last = text
        try:
            subprocess.run(["wl-copy", "--", text], timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            pass

    def send_now(self):
        """Push the current PC clipboard to the phone (used by the "Send clipboard" button)."""
        text = self._read()
        if text:
            self._last = text
            self.link.call("clip.set", {"text": text})
        return text

    @staticmethod
    def _read():
        try:
            p = subprocess.run(["wl-paste", "-n", "-t", "text"], capture_output=True, text=True, timeout=3)
            return p.stdout if p.returncode == 0 else None
        except (OSError, subprocess.TimeoutExpired):
            return None

    def _watch(self):
        # wl-paste runs `echo` on every selection change; each line is one change.
        try:
            self._proc = subprocess.Popen(["wl-paste", "-t", "text", "--watch", "echo", "x"],
                                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        except OSError:
            return
        for _ in self._proc.stdout:
            text = self._read()
            if text and text != self._last and len(text) < 1_000_000:
                self._last = text
                GLib.idle_add(self._push, text)

    def _push(self, text):
        if cfg.get("clipboard_sync") and self.link.online:
            self.link.call("clip.set", {"text": text})
