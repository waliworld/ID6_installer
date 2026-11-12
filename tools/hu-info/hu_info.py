#!/usr/bin/env python3
"""
hu_info.py - TunedByWali NBT-Evo head-unit IDENTIFY (read-only).

Capability set modeled on Viaszx/BMW_HU_Info (vehicle/HU identification, key info,
error reads, coding/FSC). This module implements only the READ-ONLY identify pass:
it writes nothing to the car. It runs AFTER SSH has been enabled (see
tools/ssh-unlock/) and connects as root over the ENET link.

What it collects (only from paths known-good for NBT-Evo ID5/ID6):
  - firmware build          /opt/sys/etc/nbt_version.txt
  - kernel / arch           uname -a           (expect QNX 6.5.0 ARM)
  - display resolution      /opt/sys/bin/LayerManagerControl get screen 0
  - framework fingerprints  sha256 of libAppFramework.A.so + libCoreFramework.A.so
  - installed gauge apps    ls /var/opt/hmi/data/ID5/Plugins
  - recent HMI errors       tail of the HMI log (best-effort, non-fatal)

From the firmware build letter it reports which FULL-SCREEN CARPLAY method applies
to this unit (coding parameter vs SSH aspect-ratio patch). That mapping is a
heuristic - CONFIRM on a reference car before relying on it.

Transport: shells out to the system `ssh` binary (OpenSSH ships with Windows 10/11
and macOS), so there is no Python package dependency. Supports an optional private
key (-i), matching BMW_HU_Info's key-based login.

VIN is NOT read here; it is reported by the diagnostic/unlock step
(tools/ssh-unlock) over HSFZ. Keep identify read-only.
"""
from __future__ import annotations
import argparse
import json
import re
import shlex
import subprocess
import sys

# Known-good NBT-Evo framework fingerprints that the gauge installer supports.
# Keep this in sync with `profiles` in source/install.go.
SUPPORTED_FRAMEWORKS = {
    # firmware build : (libAppFramework sha256, libCoreFramework sha256)
    # Values intentionally left as placeholders here; the authoritative copy lives
    # in source/install.go. identify compares against whatever install.go ships.
}

APP_FW = "/opt/hmi/ID5/lib/libAppFramework.A.so"
CORE_FW = "/opt/hmi/ID5/lib/libCoreFramework.A.so"
VERSION_FILE = "/opt/sys/etc/nbt_version.txt"
PLUGINS_DIR = "/var/opt/hmi/data/ID5/Plugins"
LM_SCREEN = "/opt/sys/bin/LayerManagerControl get screen 0"

# Full-screen CarPlay method by NBT-Evo build letter.
# Community-documented: on later builds it is a plain coding parameter; on earlier
# builds the HMI aspect ratio must be changed over SSH. Confirm per firmware.
CODING_LETTERS = set("XYZ")             # X and later: coding/FDL parameter does it
SSH_PATCH_LETTERS = set("GHIJKLMNOPQRSTUVW")  # before X: needs SSH aspect-ratio patch


def ssh_base(head: str, key: str | None, timeout: int) -> list[str]:
    cmd = [
        "ssh",
        "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=accept-new",
        "-o", f"ConnectTimeout={timeout}",
    ]
    if key:
        cmd += ["-i", key]
    cmd += [f"root@{head}"]
    return cmd


def run(head: str, key: str, timeout: int, remote: str) -> tuple[int, str, str]:
    """Run one remote command. Returns (rc, stdout, stderr). Never raises."""
    try:
        proc = subprocess.run(
            ssh_base(head, key, timeout) + [remote],
            capture_output=True, text=True, timeout=timeout + 10,
        )
        return proc.returncode, proc.stdout.strip(), proc.stderr.strip()
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    except OSError as exc:
        return 127, "", str(exc)


def carplay_method(build: str) -> dict:
    """Map a firmware build string (e.g. 'NBTevo_N18092I') to a CarPlay path."""
    m = re.search(r"NBTevo_([A-Z])", build or "", re.IGNORECASE)
    letter = m.group(1).upper() if m else None
    if letter is None:
        return {"letter": None, "fullscreen_method": "unknown",
                "note": "Could not read build letter; confirm firmware manually."}
    if letter in CODING_LETTERS:
        method = "coding"
        note = "Full-screen CarPlay is a coding/FDL parameter on this build (no SSH patch needed)."
    elif letter in SSH_PATCH_LETTERS:
        method = "ssh_patch"
        note = ("Full-screen CarPlay needs an SSH aspect-ratio patch on this build. "
                "The exact HMI config value MUST be confirmed on a reference car.")
    else:
        method = "unknown"
        note = "Build letter outside the known table; confirm the method on a reference car."
    return {"letter": letter, "fullscreen_method": method, "note": note}


def main() -> int:
    ap = argparse.ArgumentParser(description="NBT-Evo read-only identify")
    ap.add_argument("--head", required=True, help="head-unit SSH IP (169.254.x.x)")
    ap.add_argument("-i", "--key", default=None, help="optional SSH private key")
    ap.add_argument("--timeout", type=int, default=8)
    ap.add_argument("--json", action="store_true", help="emit JSON only")
    args = ap.parse_args()

    report: dict = {"head": args.head, "ok": False, "checks": {}, "warnings": []}

    # Reachability + identity
    rc, out, err = run(args.head, args.key, args.timeout, "uname -a")
    if rc != 0:
        report["warnings"].append(f"SSH to root@{args.head} failed: {err or rc}. "
                                  "Enable SSH first (tools/ssh-unlock) and confirm the IP.")
        _emit(report, args.json)
        return 2
    report["checks"]["uname"] = out
    report["checks"]["qnx_arm"] = ("QNX" in out and "arm" in out.lower())

    _, build, _ = run(args.head, args.key, args.timeout, f"cat {shlex.quote(VERSION_FILE)}")
    report["checks"]["firmware"] = build or None

    _, disp, _ = run(args.head, args.key, args.timeout, LM_SCREEN)
    report["checks"]["display"] = disp or None

    for label, path in (("libAppFramework", APP_FW), ("libCoreFramework", CORE_FW)):
        rc, out, _ = run(args.head, args.key, args.timeout,
                         f"sha256sum {shlex.quote(path)} 2>/dev/null || shasum -a 256 {shlex.quote(path)}")
        report["checks"].setdefault("framework_sha256", {})[label] = out.split()[0] if out else None

    rc, out, _ = run(args.head, args.key, args.timeout, f"ls -1 {shlex.quote(PLUGINS_DIR)} 2>/dev/null")
    report["checks"]["installed_plugins"] = [p for p in out.splitlines() if p] if out else []

    # Best-effort error read; never fatal.
    rc, out, _ = run(args.head, args.key, args.timeout,
                     "tail -n 40 /var/log/hmi.log 2>/dev/null || true")
    if out:
        report["checks"]["recent_hmi_log_tail"] = out.splitlines()[-40:]

    # CarPlay full-screen routing from firmware build letter.
    report["carplay"] = carplay_method(report["checks"].get("firmware", ""))

    # Gauge-app compatibility verdict against the installer's supported set.
    fw = report["checks"].get("framework_sha256", {})
    pair = (fw.get("libAppFramework"), fw.get("libCoreFramework"))
    report["checks"]["gauge_supported"] = any(pair == v for v in SUPPORTED_FRAMEWORKS.values()) \
        if SUPPORTED_FRAMEWORKS else None
    if report["checks"]["gauge_supported"] is None:
        report["warnings"].append("Framework allow-list not populated here; the Go installer "
                                  "performs the authoritative gauge-compatibility check.")

    report["ok"] = bool(report["checks"]["qnx_arm"])
    _emit(report, args.json)
    return 0 if report["ok"] else 1


def _emit(report: dict, as_json: bool) -> None:
    if as_json:
        print(json.dumps(report, indent=2))
        return
    c = report["checks"]
    print("=== TunedByWali · NBT-Evo Identify ===")
    print(f"Head unit     : {report['head']}")
    print(f"Kernel/arch   : {c.get('uname','?')}")
    print(f"QNX/ARM OK    : {c.get('qnx_arm')}")
    print(f"Firmware      : {c.get('firmware','?')}")
    print(f"Display       : {c.get('display','?')}")
    fw = c.get("framework_sha256", {})
    print(f"AppFramework  : {fw.get('libAppFramework','?')}")
    print(f"CoreFramework : {fw.get('libCoreFramework','?')}")
    print(f"Gauge support : {c.get('gauge_supported')}")
    print(f"Installed apps: {', '.join(c.get('installed_plugins') or []) or '(none)'}")
    cp = report.get("carplay", {})
    print(f"CarPlay FS    : {cp.get('fullscreen_method')} - {cp.get('note','')}")
    for w in report["warnings"]:
        print(f"! {w}")


if __name__ == "__main__":
    sys.exit(main())
