"""adb plumbing: USB detection, port forwarding, starting the shell helper, one-shot phone setup."""
import shlex
import shutil
import subprocess

from . import PKG

ADB = shutil.which("adb") or "adb"
LOCAL_PORT = 47901
PHONE_PORT = 47900

RUNTIME_PERMS = [
    "POST_NOTIFICATIONS", "READ_PHONE_STATE", "READ_PHONE_NUMBERS", "CALL_PHONE", "ANSWER_PHONE_CALLS",
    "READ_CALL_LOG", "WRITE_CALL_LOG", "READ_CONTACTS", "WRITE_CONTACTS", "READ_SMS", "SEND_SMS",
    "RECEIVE_SMS", "RECEIVE_MMS", "READ_MEDIA_IMAGES", "READ_MEDIA_VIDEO", "BLUETOOTH_CONNECT",
]


def run(*args, serial=None, timeout=20):
    cmd = [ADB] + (["-s", serial] if serial else []) + list(args)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, str(e)


def shell(cmd, serial=None, timeout=20):
    return run("shell", cmd, serial=serial, timeout=timeout)


def devices():
    """Serials of authorized devices that have Tandem installed."""
    code, out = run("devices", timeout=5)
    if code:
        return []
    serials = [l.split()[0] for l in out.splitlines()[1:] if l.strip().endswith("device")]
    return [s for s in serials if PKG in shell(f"pm path {PKG}", serial=s, timeout=5)[1]]


def forward(serial):
    code, _ = run("forward", f"tcp:{LOCAL_PORT}", f"tcp:{PHONE_PORT}", serial=serial, timeout=5)
    return code == 0


def start_helper(serial, apk, token, version):
    inner = (f"CLASSPATH={shlex.quote(apk)} setsid app_process /system/bin --nice-name=tandem-shell "
             f"com.cyberwolf.tandem.shell.Main {token} {version} </dev/null >/dev/null 2>&1 &")
    return shell(f"({inner})", serial=serial)


def setup_phone(serial):
    """Grant everything Tandem needs. Returns a list of (step, ok, output)."""
    steps = [(f"grant {p}", f"pm grant {PKG} android.permission.{p}") for p in RUNTIME_PERMS]
    steps += [
        ("notification access", f"cmd notification allow_listener {PKG}/{PKG}.notify.NotificationMirror"),
        ("do not disturb access", f"cmd notification allow_dnd {PKG}"),
        ("restricted settings", f"appops set {PKG} ACCESS_RESTRICTED_SETTINGS allow"),
        ("background running", f"appops set {PKG} RUN_ANY_IN_BACKGROUND allow"),
        ("battery exemption", f"dumpsys deviceidle whitelist +{PKG}"),
        ("call companion", f"cmd telecom add-or-remove-call-companion-app {PKG} 0; cmd telecom add-or-remove-call-companion-app {PKG} 1"),
        ("start service", f"am start -n {PKG}/.MainActivity"),
    ]
    results = []
    for name, cmd in steps:
        code, out = shell(cmd, serial=serial)
        results.append((name, code == 0 and "Exception" not in out and "Error" not in out, out))
    return results
