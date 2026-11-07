"""Paths and the small JSON config (pinned phone fingerprint, last host)."""
import json
import os
import subprocess
from pathlib import Path

CONFIG_DIR = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "tandem"
DATA_DIR = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "tandem"
CERT = CONFIG_DIR / "cert.pem"
KEY = CONFIG_DIR / "key.pem"
_FILE = CONFIG_DIR / "config.json"

DEFAULTS = {
    "phone_fp": None,
    "phone_name": None,
    "last_host": None,
    "downloads": str(Path.home() / "Downloads" / "Tandem"),
    "notify_mirror": True,
    "clipboard_sync": True,
    "call_audio_on_pc": True,
    "pause_media_on_call": True,
    "record_calls": False,
    "quick_replies": ["Busy right now, I'll call you back.", "On my way.", "Can't talk, text me?"],
    "muted_apps": [],
}


def load():
    try:
        return {**DEFAULTS, **json.loads(_FILE.read_text())}
    except (OSError, ValueError):
        return dict(DEFAULTS)


def save(cfg):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    tmp = _FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(cfg, indent=2))
    tmp.replace(_FILE)


def ensure_identity():
    """Self-signed EC cert the phone pins after pairing."""
    if CERT.exists() and KEY.exists():
        return
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
         "-nodes", "-keyout", str(KEY), "-out", str(CERT), "-days", "36500",
         "-subj", "/CN=Tandem PC"],
        check=True, capture_output=True,
    )
    KEY.chmod(0o600)


cfg = load()
