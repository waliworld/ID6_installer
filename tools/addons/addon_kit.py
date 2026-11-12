#!/usr/bin/env python3
"""
addon_kit.py - TunedByWali reusable add-on installer for NBT-Evo / EntryNav2.

Installs community HU add-ons (Android Auto, the AA main-menu tile, iDrive Plus
plugins, EntryNav2 AA) over the root SSH the toolbox enables - deploy, back up,
boot-hook, verify, and cleanly remove. It adapts each project's USB install flow
to a direct SSH push (no USB stick needed).

NO third-party binaries are redistributed here. Each add-on is installed from a
*kit* directory you stage from that project's own release (see addons.json
`source`). The manifest encodes the known, published install steps; the files
come from the author. Credits and licenses: ../../NOTICES.md.

CLI (also importable by toolbox.py):
    python -I addon_kit.py list
    python -I addon_kit.py status  --id <id> --head <ip> [--key K]
    python -I addon_kit.py install --id <id> --kit <dir> --head <ip> [--key K]
    python -I addon_kit.py remove  --id <id> --head <ip> [--key K]

Safety: install always backs up every target path first (timestamped, on-device),
writes a removal marker, and refuses to proceed if the kit is incomplete or SSH
is unreachable. Nothing is written without a complete kit and a reachable unit.
"""
from __future__ import annotations
import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "addons.json"
MARK_DIR = "/var/opt/tbw-addons"  # on-device: install markers + backup pointers


def _load() -> dict:
    try:
        return json.loads(MANIFEST.read_text())
    except (OSError, ValueError):
        return {}


def _ssh(head, key, remote, timeout=40):
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new",
           "-o", "ConnectTimeout=12"]
    if key:
        cmd += ["-i", key]
    cmd += [f"root@{head}", remote]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    except OSError as e:
        return 127, "", str(e)


def _scp(head, key, local: Path, remote_path: str, recursive=False, timeout=180):
    cmd = ["scp", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new"]
    if recursive:
        cmd += ["-r"]
    if key:
        cmd += ["-i", key]
    cmd += [str(local), f"root@{head}:{remote_path}"]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except (subprocess.TimeoutExpired, OSError) as e:
        return 127, "", str(e)


def _reach(head, key) -> bool:
    return _ssh(head, key, "echo ok")[1] == "ok"


# ---------------------------------------------------------------- list/status
def do_list(_a) -> dict:
    items = []
    for aid, m in _load().get("addons", {}).items():
        items.append({
            "id": aid, "name": m.get("name", aid), "desc": m.get("desc", ""),
            "license": m.get("license", "?"), "credit": m.get("credit", ""),
            "requires": m.get("requires", []), "needs_kit": True,
            "platform": m.get("platform", "nbt_evo"),
            "source": m.get("source", ""), "ready": bool(m.get("steps")),
        })
    return {"Ok": True, "Addons": items}


def do_status(a) -> dict:
    m = _load().get("addons", {}).get(a.id)
    if not m:
        return {"Ok": False, "Result": f"Unknown add-on '{a.id}'."}
    if not _reach(a.head, a.key):
        return {"Ok": False, "Result": f"Could not reach root@{a.head}. Enable SSH first."}
    rc, out, _ = _ssh(a.head, a.key, f"cat {MARK_DIR}/{shlex.quote(a.id)}.installed 2>/dev/null")
    installed = bool(out)
    return {"Ok": True, "Installed": installed,
            "Result": f"{m.get('name', a.id)}: " + ("installed" if installed else "not installed")
            + (f" (backup: {out})" if out else "")}


# -------------------------------------------------------------------- install
def do_install(a) -> dict:
    data = _load().get("addons", {})
    m = data.get(a.id)
    if not m:
        return {"Ok": False, "Result": f"Unknown add-on '{a.id}'."}
    if not m.get("steps"):
        return {"Ok": False, "Pending": True,
                "Result": f"'{a.id}' has no verified install steps yet. Add them to addons.json "
                          "after confirming on a reference car. Nothing was written."}

    kit = Path(a.kit) if a.kit else None
    files = m.get("files", [])
    if not kit or not kit.is_dir():
        return {"Ok": False, "Result": f"Stage this add-on's release into a kit folder and pass --kit. "
                                        f"Expected files: {', '.join(files) or '(see addons.json source)'}"}
    missing = [f for f in files if not (kit / f).exists()]
    if missing:
        return {"Ok": False, "Result": f"Kit is incomplete; missing: {', '.join(missing)}. "
                                        f"Download them from {m.get('source','the project release')}."}

    if not _reach(a.head, a.key):
        return {"Ok": False, "Result": f"Could not reach root@{a.head}. Enable SSH first and confirm the IP."}

    # Prerequisite notice (non-fatal).
    warnings = []
    for req in m.get("requires", []):
        warnings.append(f"Requires '{req}' already installed on the unit.")

    # 1) workspace + backup
    stage = f"/tmp/tbw-{a.id}"
    _ssh(a.head, a.key, m.get("remount", "mount -o remount rw /net/hu-omap/fs/sda0 2>/dev/null; true"))
    rc, bdir, _ = _ssh(a.head, a.key,
        f"mkdir -p {MARK_DIR} {stage} && d={MARK_DIR}/{shlex.quote(a.id)}.$(date +%Y%m%d-%H%M%S) && mkdir -p \"$d\" && echo \"$d\"")
    if rc != 0 or not bdir:
        return {"Ok": False, "Result": "Could not create an on-device workspace/backup; aborted before writing."}
    bdir = bdir.splitlines()[-1]
    for p in m.get("backup_paths", []):
        _ssh(a.head, a.key, f"cp -a {shlex.quote(p)} {shlex.quote(bdir)}/ 2>/dev/null || true")

    # 2) push the kit (whole folder for tree-based add-ons, else listed files)
    if m.get("upload") == "dir":
        _ssh(a.head, a.key, f"rm -rf {stage} && mkdir -p {stage}")
        rc, _, err = _scp(a.head, a.key, kit, stage, recursive=True)
        if rc != 0:
            return {"Ok": False, "Backup": bdir, "Result": f"Upload of kit folder failed ({err}); nothing installed."}
        stage = f"{stage}/{kit.name}"
    else:
        for f in files:
            rc, _, err = _scp(a.head, a.key, kit / f, f"{stage}/{Path(f).name}")
            if rc != 0:
                return {"Ok": False, "Backup": bdir, "Result": f"Upload of {f} failed ({err}); nothing installed."}

    # 3) run verified install steps
    ok, log = True, []
    for raw in m["steps"]:
        remote = raw.format(stage=stage, backup=bdir)
        rc, out, err = _ssh(a.head, a.key, remote, timeout=60)
        log.append(f"$ {remote}\n{out or err}".strip())
        if rc != 0:
            ok = False
            log.append(f"! step failed rc={rc}")
            break

    # 4) record install marker
    if ok:
        _ssh(a.head, a.key, f"printf %s {shlex.quote(bdir)} > {MARK_DIR}/{shlex.quote(a.id)}.installed")

    return {"Ok": ok, "Backup": bdir, "Warnings": warnings,
            "Result": (f"{m.get('name', a.id)} installed. Restart iDrive (hold power ~30s) and verify on the car."
                       if ok else f"Install step failed; restore from the backup at {bdir}."),
            "Log": log}


# --------------------------------------------------------------------- remove
def do_remove(a) -> dict:
    m = _load().get("addons", {}).get(a.id)
    if not m:
        return {"Ok": False, "Result": f"Unknown add-on '{a.id}'."}
    if not _reach(a.head, a.key):
        return {"Ok": False, "Result": f"Could not reach root@{a.head}. Enable SSH first."}
    _ssh(a.head, a.key, m.get("remount", "mount -o remount rw /net/hu-omap/fs/sda0 2>/dev/null; true"))
    ok, log = True, []
    for raw in m.get("remove_steps", []):
        rc, out, err = _ssh(a.head, a.key, raw.format(mark=MARK_DIR), timeout=60)
        log.append(f"$ {raw}\n{out or err}".strip())
        if rc != 0:
            ok = False
            break
    _ssh(a.head, a.key, f"rm -f {MARK_DIR}/{shlex.quote(a.id)}.installed")
    return {"Ok": ok, "Result": (f"{m.get('name', a.id)} removed; restart iDrive to finish."
                                  if ok else "Remove step failed; see Log."),
            "Log": log}


def main() -> int:
    ap = argparse.ArgumentParser(description="TunedByWali add-on installer")
    ap.add_argument("cmd", choices=["list", "status", "install", "remove"])
    ap.add_argument("--id")
    ap.add_argument("--kit")
    ap.add_argument("--head")
    ap.add_argument("--key")
    a = ap.parse_args()
    fn = {"list": do_list, "status": do_status, "install": do_install, "remove": do_remove}[a.cmd]
    if a.cmd != "list" and (not a.id or not a.head):
        print(json.dumps({"Ok": False, "Result": "need --id and --head"}))
        return 2
    print(json.dumps(fn(a)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
