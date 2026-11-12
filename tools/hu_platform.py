#!/usr/bin/env python3
"""
platform.py - detect WHICH BMW head unit is connected and gate capabilities to it.

NBT Evo (HU_NBT2) and EntryNav2 (HU_ENTRY / "EntryEVO") are different units: the SSH
rooting, coding keys, and container formats differ. So the toolbox identifies the unit
first and only exposes what that platform actually supports.

Key fact this encodes: on NBT Evo, "enable SSH" == flipping OPT_DEBUG (one mechanism,
via the bundled flasher). On EntryNav2 the root path is different (root password supplied
separately), so OPT_DEBUG flashing is NOT offered there.

Detection signals (best-effort, in order):
  - an explicit firmware/version string (NBTevo_* -> nbt_evo; EntryEVO/EntryNav -> entrynav2)
  - a reachable root SSH: read /opt/sys/etc/nbt_version.txt
  - otherwise: unknown (identify over the car, or type the version)
"""
from __future__ import annotations
import os
import re
import subprocess
from pathlib import Path

# ---- capability map: what each platform supports -------------------------------
CAPABILITIES = {
    "nbt_evo": {
        "label": "NBT Evo (iDrive 5/6)",
        "ssh_method": "opt_debug",       # the bundled flasher
        "features": [
            "enable_ssh", "identify", "gauges", "coding", "fsc",
            "carplay_activate", "carplay_fullscreen",
            "addon:nbt-aad", "addon:aa-menu-tile", "addon:idrive-plus-plugins",
            "vehicle_models", "plugins", "flash_patch",
        ],
    },
    "entrynav2": {
        "label": "EntryNav2 (EntryEVO)",
        "ssh_method": "root_password",   # supplied by the operator; no OPT_DEBUG flash
        "features": ["identify", "addon:entry-aad"],
        "note": "EntryNav2 is a different HU: our OPT_DEBUG flasher, NBT Evo gauges, and NBT coding "
                "do not apply. Android Auto (entry-aad) is the supported add-on here.",
    },
    "unknown": {
        "label": "Not identified yet",
        "ssh_method": None,
        "features": ["identify"],
        "note": "Connect over ENET and identify, or type the iDrive version, to unlock the right tools.",
    },
}


def classify(firmware: str) -> str:
    f = (firmware or "").upper()
    if "NBTEVO_" in f or re.search(r"\bNBT ?EVO\b", f):
        return "nbt_evo"
    if "ENTRYEVO" in f or "ENTRYNAV" in f or "HU_ENTRY" in f:
        return "entrynav2"
    return "unknown"


def _ssh_read(head: str, key, remote: str, timeout=10):
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new",
           "-o", "ConnectTimeout=8"]
    if key:
        cmd += ["-i", key]
    cmd += [f"root@{head}", remote]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.stdout.strip() if p.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def detect(body: dict) -> dict:
    firmware = (body.get("Version") or body.get("firmware") or "").strip()
    vin = (body.get("Vin") or body.get("vin") or "").strip()
    head = body.get("Head") or body.get("head")
    key = body.get("Key") or body.get("key")

    # Try the car if we have nothing and SSH is up.
    if not firmware and head:
        firmware = _ssh_read(head, key, "cat /opt/sys/etc/nbt_version.txt 2>/dev/null") or \
                   _ssh_read(head, key, "uname -a")

    platform = classify(firmware)
    cap = CAPABILITIES[platform]
    return {
        "Ok": platform != "unknown",
        "Platform": platform,
        "Label": cap["label"],
        "SshMethod": cap["ssh_method"],
        "Firmware": firmware or None,
        "Vin": vin or None,
        "Features": cap["features"],
        "Note": cap.get("note", ""),
        "Result": f"Detected {cap['label']}." if platform != "unknown"
                  else "Head unit not identified yet.",
    }


# ---- PSdZData: locate the operator's existing copy (E-Sys / ISTA) ---------------
PSDZ_CANDIDATES = [
    os.environ.get("NBTEVO_PSDZDATA", ""),
    r"C:\Data\psdzdata",
    r"C:\EC-Apps\PSdZ\psdzdata",
    r"C:\Program Files\PSdZ\psdzdata",
    r"C:\ProgramData\BMW\ISPI\data\PSdZ\psdzdata",
    os.path.expanduser("~/psdzdata"),
    "/opt/psdzdata",
]


def psdzdata_locate(_body: dict | None = None) -> dict:
    found = None
    for c in PSDZ_CANDIDATES:
        if c and Path(c).is_dir():
            found = c
            break
    if found:
        return {"Ok": True, "Path": found,
                "Result": f"Using PSdZData already on this system: {found}"}
    return {"Ok": False, "Path": None,
            "Result": "PSdZData not found in the usual E-Sys/ISTA locations. Set NBTEVO_PSDZDATA "
                      "to your installed copy (coding/FSC writes read it directly from the system)."}
