#!/usr/bin/env python3
"""
toolkit_bridge.py - bridge from the TBW toolbox to the vendored MIT
nbt-evo-toolkit (third_party/nbt-evo-toolkit), which provides the real
CODING and FSC engines.

This replaces the earlier 'gated placeholder' CarPlay path with the toolkit's
honest mechanism:
  - full-screen / feature coding: the toolkit signs coding so it survives reboot
    (capabilities/coding) - 1481 mapped params incl. the CarPlay coding bit.
  - CarPlay (and other) FSC: the toolkit's blobB trust-anchor model
    (capabilities/fsc) - mint/verify run locally; the car-write (blobB patch +
    storeFSC apply) are the toolkit's documented runbooks.

HONEST BOUNDARY
  - Minting / verifying / self-test / catalog run offline here (no car).
  - Coding/FSC *writes* need the operator's own PSdZData and the toolkit's
    owner-VIN consent gate (NBTEVO_OWN_VIN == connected car's VIN), and a car.
    The bridge passes those through; it never bypasses the gate.
See docs/CODING_FSC.md. Credits/license: NOTICES.md (toolkit is MIT).
"""
from __future__ import annotations
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOOLKIT = Path(os.environ.get("NBTEVO_TOOLKIT", REPO / "third_party" / "nbt-evo-toolkit"))
FSC_TOOL = TOOLKIT / "capabilities" / "fsc" / "tools" / "fsc.py"
CATALOG = TOOLKIT / "capabilities" / "fsc" / "catalog.md"


def present() -> bool:
    return FSC_TOOL.exists()


def _env(body: dict | None = None) -> dict:
    e = dict(os.environ)
    e["PYTHONPATH"] = str(TOOLKIT) + os.pathsep + e.get("PYTHONPATH", "")
    # Owner-consent: set NBTEVO_OWN_VIN only when the operator confirmed authorization
    # for this VIN (the toolkit still re-checks it against the connected car on writes).
    if body and body.get("Authorized") and body.get("OwnVin"):
        e["NBTEVO_OWN_VIN"] = str(body["OwnVin"]).strip().upper()
    return e


def _run(args: list[str], timeout=60, body: dict | None = None):
    try:
        p = subprocess.run([sys.executable, *args], cwd=str(TOOLKIT), env=_env(body),
                           capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except subprocess.TimeoutExpired:
        return 124, "", "timed out"
    except OSError as e:
        return 127, "", str(e)


def _missing() -> dict:
    return {"Ok": False, "Result": "nbt-evo-toolkit not found. Expected at third_party/nbt-evo-toolkit "
            "or $NBTEVO_TOOLKIT. It's MIT-licensed; vendored with this toolbox."}


# ------------------------------------------------------------- local (no car)
def fsc_selftest(_body=None) -> dict:
    if not present():
        return _missing()
    rc, out, err = _run(["-m", "capabilities.fsc.validate.check_fsc"], timeout=60)
    text = (out + "\n" + err).strip()
    return {"Ok": rc == 0, "Result": ("FSC engine self-test passed (mint/verify/modulus-recovery)."
            if rc == 0 else "FSC self-test failed.") + ("\n" + text[-600:] if text else "")}


def fsc_catalog(_body=None) -> dict:
    if not CATALOG.exists():
        return _missing()
    feats = []
    for line in CATALOG.read_text(errors="ignore").splitlines():
        m = re.match(r"\|\s*`?(0x[0-9A-Fa-f]+)`?\s*\|\s*([0-9]+)?\s*\|\s*\**([^|*]+?)\**\s*\|\s*([^|]*)\|", line)
        if m:
            feats.append({"appno": m.group(1), "swid": (m.group(2) or "").strip(),
                          "name": m.group(3).strip(), "note": m.group(4).strip()})
    return {"Ok": bool(feats), "Features": feats}


def fsc_keygen(body: dict) -> dict:
    if not present():
        return _missing()
    outdir = Path(body.get("dir") or (REPO / "fsc-keys"))
    outdir.mkdir(parents=True, exist_ok=True)
    key, blob = outdir / "owner_key.json", outdir / "owner_blobB.bin"
    rc, out, err = _run([str(FSC_TOOL), "keygen", "-o", str(key), "--blobb", str(blob)])
    return {"Ok": rc == 0, "Key": str(key), "Blobb": str(blob),
            "Result": (out or "owner trust anchor generated") if rc == 0 else (err or "keygen failed")}


def fsc_mint(body: dict) -> dict:
    if not present():
        return _missing()
    appno = body.get("appno", "0x0143")
    key = body.get("key") or str(REPO / "fsc-keys" / "owner_key.json")
    if not Path(key).exists():
        return {"Ok": False, "Result": f"No owner key at {key}. Run fsc-keygen first (your root of trust)."}
    out = body.get("out") or str(REPO / "fsc-keys" / f"feature_{appno}.fsc")
    rc, so, se = _run([str(FSC_TOOL), "mint", "--key", key, "--appno", appno, "-o", out])
    return {"Ok": rc == 0, "Fsc": out if rc == 0 else None,
            "Result": (so or f"minted {appno}") if rc == 0 else (se or "mint failed")}


# ------------------------------------------------------------- car (gated)
def _toolkit_mod(mod: str, body: dict) -> dict:
    """Run a car-touching coding tool; surfaces the toolkit's own gate messages."""
    if not present():
        return _missing()
    head = body.get("Head") or body.get("head")
    if not head:
        return {"Ok": False, "Result": "Head unit IP required."}
    args = ["-m", mod, "--host", head]
    if body.get("out"):
        args += ["-o", body["out"]]
    rc, out, err = _run(args, timeout=90, body=body)
    text = (out + "\n" + err).strip()
    if "NBTEVO_OWN_VIN" in text or "consent" in text.lower():
        return {"Ok": False, "Gated": True,
                "Result": "Blocked by the toolkit's owner-consent gate: set NBTEVO_OWN_VIN to the VIN of "
                          "the car you own/are authorized to modify (must match the connected car). " + text[-300:]}
    if rc != 0 and ("Traceback" in text or "Error" in text or "refused" in text.lower() or "timed out" in text):
        last = next((ln for ln in reversed(text.splitlines()) if ln.strip()), "")
        return {"Ok": False,
                "Result": "Coding read needs a reachable car over ENET (and, for key recovery, your PSdZData "
                          "and NBTEVO_OWN_VIN). Toolkit reported: " + last[-200:]}
    return {"Ok": rc == 0, "Result": text[-900:] or ("done" if rc == 0 else "no output")}


def coding_identify(body: dict) -> dict:
    return _toolkit_mod("capabilities.coding.tools.identify_btld", body)


def coding_backup(body: dict) -> dict:
    body = dict(body)
    body.setdefault("out", str(REPO / "fsc-keys" / "coding-backup.ncd"))
    return _toolkit_mod("capabilities.coding.tools.read_coding", body)


DEFS = TOOLKIT / "capabilities" / "coding" / "definitions" / "cafd_00001ef6_definitions.json"
import json as _json


def resolve_token(token: str) -> dict | None:
    """Map a coding token (e.g. CARPLAY_FULLSCREEN) to its coordinate from the toolkit
    definitions. Reference CAFD 00001EF6 — RE-VERIFY for a unit whose CAFD differs."""
    if not DEFS.exists():
        return None
    try:
        for o in _json.loads(DEFS.read_text()):
            if str(o.get("token", "")).upper() == token.upper():
                mask = str(o.get("mask", "0"))
                mval = int(mask[:-1], 2) if mask.endswith("b") else int(mask, 0)
                field = str(o.get("field", ""))
                shift = int(re.sub(r"\D", "", field) or 0) if field.startswith("bit") else 0
                return {"token": o["token"], "name": o.get("name"), "dg": str(o.get("dg")),
                        "byte": o.get("byte"), "mask": hex(mval), "shift": shift,
                        "values": o.get("values", {}), "confidence": o.get("confidence"),
                        "fa_gate": o.get("fa_gate")}
    except (ValueError, OSError, KeyError):
        return None
    return None


def coding_set(body: dict) -> dict:
    """Resolve a coding token to its coordinate, enforce a backup, and return the exact
    verified write sequence. Does NOT auto-commit: coding writes need your PSdZData, the
    recovered signing key, your L3 key, and the owner-VIN consent gate (the toolkit enforces
    all of these on the actual write). Full-screen CarPlay = CARPLAY_FULLSCREEN; CarPlay
    enable bit = CARPLAY_SWT."""
    if not present():
        return _missing()
    token = body.get("token", "CARPLAY_FULLSCREEN")
    value = int(body.get("value", 1))
    head = body.get("Head") or body.get("head")
    r = resolve_token(token)
    if not r:
        return {"Ok": False, "Result": f"Coding token '{token}' not found in the reference definitions."}
    setarg = f"0x{r['dg']},{r['byte']},{value},{r['mask']},{r['shift']}"

    # Enforce read/backup first (write-proofing).
    backup = None
    if head:
        b = coding_backup(dict(body, Head=head))
        backup = b.get("Result")

    plan = [
        "python -m capabilities.coding.tools.recover_key --btld <BTLD> -o key.json   # from YOUR PSdZData, once",
        "python -m capabilities.coding.tools.read_coding --host <HU> -o backup.ncd    # backup (done if car reachable)",
        f"python -m capabilities.coding.tools.sign_ncd backup.ncd modified.ncd --key key.json --set {setarg}",
        "python -m capabilities.coding.tools.write_coding --host <HU> --ncd modified.ncd --key key.json --l3-key <L3>  # rehearse",
        "python -m capabilities.coding.tools.write_coding --host <HU> --ncd modified.ncd --key key.json --l3-key <L3> --commit",
        "python -m capabilities.coding.tools.read_coding --host <HU> -o after.ncd     # read back + diff to verify",
    ]
    return {"Ok": False, "Pending": True, "Token": r["token"], "Name": r["name"],
            "Coord": f"dg{r['dg']}/byte{r['byte']}/{r['mask']} <- {value}", "SetArg": setarg,
            "Backup": backup, "Plan": plan, "Confidence": r.get("confidence"),
            "Result": f"Resolved {r['token']} ({r['name']}) to {setarg}. "
                      + (f"Coding backed up: {backup}. " if backup else "Connect the car to auto-backup first. ")
                      + "The signed write needs your PSdZData + recovered key + L3 key + owner-VIN consent "
                      "(toolkit-enforced). Coordinates are from CAFD 00001EF6 — re-verify if your CAFD differs. "
                      "See docs/CODING_FSC.md."}


def carplay_activate(body: dict) -> dict:
    """Ordered, honest CarPlay activation plan via the toolkit (examples/carplay.md).
    Runs the local FSC mint now; the car-write steps are the toolkit's runbooks."""
    if not present():
        return _missing()
    steps = [
        "1. Back up coding (coding-backup) and current FSCs - always first.",
        "2. Mint the CarPlay FSC 0x0143 under YOUR trust anchor (fsc-keygen + fsc-mint) - runs locally now.",
        "3. Install your blobB trust anchor into NBTCarHU (flash_and_patching runbook).",
        "4. Apply the minted FSC over the Swt service (storeFSC runbook).",
        "5. Set the CARPLAY_SWT coding bit via signed coding write (capabilities/coding) - needs your PSdZData + consent VIN.",
        "6. Reboot; confirm CarPlay appears. Revert from the backup if the HMI doesn't come up.",
    ]
    mint = fsc_mint({"appno": "0x0143", **({"key": body["key"]} if body.get("key") else {})})
    return {"Ok": False, "Pending": True, "Plan": steps, "Mint": mint,
            "Result": "CarPlay activation is a real, ordered procedure now (FSC 0x0143 + CARPLAY_SWT coding). "
                      "Local FSC mint " + ("succeeded. " if mint.get("Ok") else "needs an owner key (fsc-keygen). ")
                      + "The two car-write steps (blobB patch, storeFSC apply) follow the toolkit runbooks; "
                      "coding + FSC writes need your PSdZData and the owner-VIN consent gate. See docs/CODING_FSC.md."}
