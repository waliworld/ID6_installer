#!/usr/bin/env python3
"""
toolbox.py - TunedByWali NBT-Evo Toolbox backend (self-reliant).

Single entry point the app calls for the stages the Go gauge engine doesn't own.
Reads a JSON body on stdin, writes a JSON result on stdout:

    python -I toolbox.py <op>  < body.json

Ops:
  selfcheck         - environment + tool self-test (no car needed)
  unlock            - enable/revert SSH via the vendored OPT_DEBUG flasher
                      body: {selftest|commit|revert: true, Gateway, Head}
  carplay           - full-screen CarPlay aspect patch (gated by verified profile)
  carplay-activate  - enable CarPlay on a unit that has none (FSC, gated)

All logic is stdlib-only and runs locally. The flasher and CarPlay steps are
gated: they do nothing destructive without a passing self-test / verified profile.
"""
from __future__ import annotations
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLASHER = HERE / "ssh-unlock" / "nbt_optdebug_flash.py"
HU_INFO = HERE / "hu-info" / "hu_info.py"
PY = sys.executable or "python3"

sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "addons"))
try:
    import carplay_fsc
except ImportError:
    import carplay_fsc
try:
    import addon_kit
except ImportError:
    addon_kit = None

from types import SimpleNamespace


def _py(script: Path, args: list[str], timeout: int):
    """Run a bundled python tool isolated (-I). Returns (rc, stdout, stderr)."""
    try:
        p = subprocess.run([PY, "-I", str(script), *args],
                           capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timed out"
    except OSError as e:
        return 127, "", str(e)


# ----------------------------------------------------------------- selfcheck
def _enet_link() -> str | None:
    """Best-effort: find a 169.254.x.x link-local address (the ENET link)."""
    try:
        if os.name == "nt":
            out = subprocess.run(["ipconfig"], capture_output=True, text=True, timeout=8).stdout
        else:
            cmd = ["ip", "-4", "addr"] if shutil.which("ip") else ["ifconfig"]
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=8).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"169\.254\.\d{1,3}\.\d{1,3}", out or "")
    return m.group(0) if m else None


def selfcheck(_body: dict) -> dict:
    checks: dict = {}
    warn: list[str] = []

    checks["python"] = sys.version.split()[0]
    checks["ssh"] = bool(shutil.which("ssh"))
    if not checks["ssh"]:
        warn.append("OpenSSH 'ssh' not found. On Windows: Settings > Apps > Optional features > OpenSSH Client.")

    checks["tools_present"] = {
        "flasher": FLASHER.exists(),
        "hu_info": HU_INFO.exists(),
        "carplay_fsc": (HERE / "carplay_fsc.py").exists(),
    }
    for name, ok in checks["tools_present"].items():
        if not ok:
            warn.append(f"Bundled tool missing: {name}.")

    link = _enet_link()
    checks["enet_link"] = link
    if not link:
        warn.append("No 169.254.x.x adapter found. Plug in the ENET cable and wait for a link-local address.")

    # Offline flasher self-test: proves the unlock engine + key + blobs are intact.
    if FLASHER.exists():
        rc, out, err = _py(FLASHER, ["--selftest"], timeout=30)
        passed = "ALL PASS" in (out + err)
        checks["flasher_selftest"] = passed
        if not passed:
            warn.append("SSH flasher self-test did not pass - do not attempt to enable SSH.")
    else:
        checks["flasher_selftest"] = False

    ok = checks["ssh"] and all(checks["tools_present"].values()) and checks["flasher_selftest"]
    return {"Ok": bool(ok), "Checks": checks, "Warnings": warn,
            "Result": "Ready." if ok else "Self-check found items to resolve (see Warnings)."}


# -------------------------------------------------------------------- unlock
def unlock(body: dict) -> dict:
    gateway = (body.get("Gateway") or "").strip()
    if body.get("selftest"):
        rc, out, err = _py(FLASHER, ["--selftest"], timeout=30)
        text = (out + err).strip()
        return {"Pass": "ALL PASS" in text, "Result": text[-800:] or "self-test produced no output"}

    args: list[str] = []
    if body.get("commit"):
        args = ["--commit"]
    elif body.get("revert"):
        args = ["--revert"]
    else:
        return {"Ok": False, "Result": "unlock needs selftest, commit, or revert."}

    if gateway:
        args += ["--host", gateway, "--no-discover"]
    # Keep under the Go server's 4-minute write timeout.
    rc, out, err = _py(FLASHER, args, timeout=200)
    text = (out + "\n" + err).strip()
    vin = re.search(r"\b([A-HJ-NPR-Z0-9]{17})\b", text)
    ok = rc == 0 and "aborted" not in text.lower() and "fail" not in text.lower()
    res = {"Ok": bool(ok), "Result": text[-1200:] or ("done" if ok else "no output")}
    if vin:
        res["Vin"] = vin.group(1)
    return res


# ------------------------------------------------------------------- carplay
def carplay(body: dict) -> dict:
    return carplay_fsc.fullscreen(body)


def carplay_activate(body: dict) -> dict:
    return carplay_fsc.activate(body)


# -------------------------------------------------------------------- add-ons
def _addon_args(body: dict) -> SimpleNamespace:
    return SimpleNamespace(
        id=body.get("id"), kit=body.get("kit"),
        head=body.get("Head") or body.get("head"),
        key=body.get("Key") or body.get("key"))


def _addon(fn_name: str, body: dict) -> dict:
    if addon_kit is None:
        return {"Ok": False, "Result": "Add-on framework not available (tools/addons missing)."}
    return getattr(addon_kit, fn_name)(_addon_args(body))


def addons_list(body: dict) -> dict:
    return _addon("do_list", body)


def addon_install(body: dict) -> dict:
    return _addon("do_install", body)


def addon_remove(body: dict) -> dict:
    return _addon("do_remove", body)


def addon_status(body: dict) -> dict:
    return _addon("do_status", body)


OPS = {
    "selfcheck": selfcheck,
    "unlock": unlock,
    "carplay": carplay,
    "carplay-activate": carplay_activate,
    "addons-list": addons_list,
    "addon-install": addon_install,
    "addon-remove": addon_remove,
    "addon-status": addon_status,
}


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] not in OPS:
        print(json.dumps({"Result": f"unknown op; expected one of {sorted(OPS)}"}))
        return 2
    raw = sys.stdin.read().strip()
    try:
        body = json.loads(raw) if raw else {}
    except ValueError:
        print(json.dumps({"Result": "invalid JSON body"}))
        return 2
    try:
        print(json.dumps(OPS[sys.argv[1]](body)))
    except Exception as e:  # never crash the caller; report as a result
        print(json.dumps({"Ok": False, "Result": f"backend error: {e}"}))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
