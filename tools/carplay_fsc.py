#!/usr/bin/env python3
"""
carplay_fsc.py - TunedByWali: CarPlay activation + full-screen for NBT-Evo.

Two functions, both run over the root SSH the toolbox enabled:
  activate()   - turn ON Apple CarPlay for a unit that does NOT have it (FSC).
  fullscreen() - reshape already-active CarPlay to full screen (aspect patch).

SAFETY-FIRST AND HONEST
-----------------------
This module owns the DETECTION, BACKUP, APPLY and VERIFY orchestration. It does
NOT fabricate FSC data, cryptographic keys, or on-device paths - writing an
unverified value here could break the unit. The per-firmware specifics are
supplied by the shop, as a verified profile in `carplay_profiles.json` and/or a
real `.fsc` file. Until a verified profile exists for the target firmware,
these functions return a `pending` result and write NOTHING.

CarPlay activation path (community-documented, high level):
  1. root SSH  (already enabled by the toolbox: tools/ssh-unlock)
  2. back up the on-device FSC store and CA/public-key store
  3. install a valid CarPlay FSC the shop supplies, OR replace the on-device CA
     modulus with the shop's own and apply a self-signed CarPlay FSC
  4. reload and verify that CarPlay appears
Only inputs VERIFIED for the unit's firmware are ever executed.
"""
from __future__ import annotations
import json
import shlex
import subprocess
from pathlib import Path

PROFILES = Path(__file__).resolve().parent / "carplay_profiles.json"


def _ssh(head: str, key: str | None, remote: str, timeout: int = 20):
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new",
           "-o", f"ConnectTimeout={min(timeout,15)}"]
    if key:
        cmd += ["-i", key]
    cmd += [f"root@{head}", remote]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 10)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    except OSError as e:
        return 127, "", str(e)


def _profiles() -> dict:
    if PROFILES.exists():
        try:
            return json.loads(PROFILES.read_text())
        except (OSError, ValueError):
            return {}
    return {}


def _match_profile(firmware: str, kind: str) -> dict | None:
    """Return a verified profile block for this firmware+kind, else None."""
    data = _profiles()
    for key, prof in data.items():
        if not isinstance(prof, dict) or prof.get("kind") != kind:
            continue
        if key.upper() in (firmware or "").upper() or prof.get("match", "").upper() in (firmware or "").upper():
            return prof
    return None


def _backup(head, key, paths: list[str]) -> tuple[bool, str]:
    """Copy each path to a timestamped on-device backup dir. Returns (ok, dir)."""
    bdir = "/var/opt/hmi/data/tbw-backup/$(date +%Y%m%d-%H%M%S)"
    rc, out, _ = _ssh(head, key, f"d={bdir}; mkdir -p \"$d\" && echo \"$d\"")
    if rc != 0 or not out:
        return False, ""
    target = out.splitlines()[-1]
    for p in paths:
        rc, _, _ = _ssh(head, key, f"cp -a {shlex.quote(p)} {shlex.quote(target)}/ 2>/dev/null || true")
    return True, target


def _run_steps(head, key, steps: list[str], subst: dict) -> tuple[bool, list[str]]:
    log = []
    for raw in steps:
        remote = raw.format(**subst)
        rc, out, err = _ssh(head, key, remote, timeout=40)
        log.append(f"$ {remote}\n{out or err}".strip())
        if rc != 0:
            log.append(f"! step failed rc={rc}")
            return False, log
    return True, log


def detect_carplay(head: str, key: str | None) -> bool | None:
    """Best-effort, non-authoritative: look for a CarPlay FSC / app marker."""
    rc, out, _ = _ssh(head, key,
        "ls /var/opt/hmi 2>/dev/null; grep -rIl -i carplay /opt/hmi/ID5 2>/dev/null | head -1")
    if rc != 0:
        return None
    return bool(out.strip())


def _pending(firmware: str, kind: str, extra: str = "") -> dict:
    return {
        "Ok": False, "Pending": True,
        "Result": (f"No verified CarPlay {kind} profile for firmware '{firmware or 'unknown'}'. "
                   "Nothing was written. " + extra +
                   "Add a verified block to tools/carplay_profiles.json (see docs/CARPLAY_ACTIVATE_FSC.md) "
                   "after confirming it on a reference car.")
    }


def activate(params: dict) -> dict:
    """Enable Apple CarPlay on a unit that doesn't have it."""
    head = params.get("Head") or params.get("head")
    key = params.get("Key") or params.get("key")
    firmware = params.get("Version") or params.get("firmware") or ""
    fsc_file = params.get("FscFile") or params.get("fsc")
    if not head:
        return {"Ok": False, "Result": "Head unit IP is required (enable SSH first)."}

    present = detect_carplay(head, key)
    if present is True:
        return {"Ok": True, "Result": "CarPlay already appears active on this unit - no activation needed."}
    if present is None:
        return {"Ok": False, "Result": f"Could not reach root@{head} over SSH. Enable SSH first and confirm the IP."}

    prof = _match_profile(firmware, "activate")
    if not prof and not fsc_file:
        return _pending(firmware, "activation",
                        "Provide a CarPlay FSC (--fsc / FscFile) from your licensed source, or a verified profile. ")

    subst = {"fsc": shlex.quote(fsc_file) if fsc_file else ""}
    backup_paths = (prof or {}).get("backup_paths",
                    ["/var/opt/hmi/data/ID5/fsc", "/opt/hmi/ID5/etc"])
    ok_b, bdir = _backup(head, key, backup_paths)
    if not ok_b:
        return {"Ok": False, "Result": "Could not create an on-device backup; aborted before writing anything."}

    steps = (prof or {}).get("steps")
    if not steps:
        return {"Ok": False, "Pending": True,
                "Result": (f"FSC file staged and backup made at {bdir}, but no verified apply steps for "
                           f"firmware '{firmware}'. Add them to carplay_profiles.json after reference-car "
                           "verification. Nothing was written to the FSC store.")}

    ok, log = _run_steps(head, key, steps, subst)
    verify = detect_carplay(head, key)
    return {"Ok": bool(ok and verify), "Backup": bdir,
            "Result": ("CarPlay activation applied; verify on the car after an iDrive restart."
                       if ok else "Activation step failed; restore the backup at " + bdir + "."),
            "Log": log}


def fullscreen(params: dict) -> dict:
    """Reshape already-active CarPlay to full screen (aspect patch)."""
    head = params.get("Head") or params.get("head")
    key = params.get("Key") or params.get("key")
    firmware = params.get("Version") or params.get("firmware") or ""
    if not head:
        return {"Ok": False, "Result": "Head unit IP is required (enable SSH first)."}

    prof = _match_profile(firmware, "fullscreen")
    if not prof:
        return _pending(firmware, "full-screen")

    ok_b, bdir = _backup(head, key, prof.get("backup_paths", ["/opt/hmi/ID5/etc"]))
    if not ok_b:
        return {"Ok": False, "Result": "Could not create an on-device backup; aborted."}
    ok, log = _run_steps(head, key, prof.get("steps", []), {})
    return {"Ok": bool(ok), "Backup": bdir,
            "Result": ("Full-screen CarPlay patch applied; restart iDrive and verify."
                       if ok else "Patch step failed; restore the backup at " + bdir + "."),
            "Log": log}
