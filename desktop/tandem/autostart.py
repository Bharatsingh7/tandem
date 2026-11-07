"""Start at login via an XDG autostart entry (Hyprland users: uwsm/dex honour these) or a systemd user unit."""
import shutil
import subprocess
from pathlib import Path

UNIT = Path.home() / ".config/systemd/user/tandem.service"
LAUNCHER = Path.home() / ".local/bin/tandem"


def autostart_enabled():
    try:
        return subprocess.run(["systemctl", "--user", "is-enabled", "tandem.service"],
                              capture_output=True, text=True).stdout.strip() == "enabled"
    except OSError:
        return False


def set_autostart(on):
    if on:
        UNIT.parent.mkdir(parents=True, exist_ok=True)
        exe = str(LAUNCHER) if LAUNCHER.exists() else shutil.which("tandem") or "tandem"
        UNIT.write_text(
            "[Unit]\nDescription=Tandem phone link\nPartOf=graphical-session.target\nAfter=graphical-session.target\n\n"
            f"[Service]\nExecStart={exe} --background\nRestart=on-failure\nRestartSec=5\n\n"
            "[Install]\nWantedBy=graphical-session.target\n")
        subprocess.run(["systemctl", "--user", "daemon-reload"])
        subprocess.run(["systemctl", "--user", "enable", "tandem.service"])
    else:
        subprocess.run(["systemctl", "--user", "disable", "tandem.service"])
