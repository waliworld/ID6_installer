#!/usr/bin/env python3
"""
ssh_gate.py -- pre-flight compatibility gate for every rooting / install style
in this toolbox.

WHY THIS EXISTS
---------------
The OPT_DEBUG SSH unlock writes a `starter.cfg` that fits ONE software family:
NBT Evo 007.xxx. On any other line the flasher's own header is blunt --

    "a bad/wrong-version starter.cfg fails the NEXT boot"

-- and that is a brick, not a dark screen.

Before this module the only version data in the whole program came from
`/opt/sys/etc/nbt_version.txt`, read **over SSH** -- which only exists AFTER a
successful unlock. So the unlock itself ran with no version check at all: it
entered flash mode first (`31 01 0F0C 03`) and discovered the incompatibility
afterwards, by which point the unit was already in flash mode and, on a failed
exit, stuck on a black screen. That is the upstream bug
(Xayanix/nbt-evo-ssh-unlocker issue #3) and it is an ORDERING bug.

This gate inverts the default: **DENY unless the unit is positively proven
supported**, decided before anything touches EnergyMode.

THE ONE RULE
------------
A build letter can only ever DENY. It can never ALLOW.
Only a confirmed software line (e.g. `007_051_002`) can ALLOW. If we cannot
prove the line, the answer is DENY-UNKNOWN and the operator must read it from
E-Sys / ISTA and pass it in.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

# --------------------------------------------------------------------------
# Build-code grammar:  <LETTER><YY><DDD><REV>     e.g. N18032I, Y24073J
#   LETTER  firmware branch       A-F = iDrive 4, H-Y = iDrive 5/6
#   YY      model year of build   18 -> 2018
#   DDD     day-of-year of build  032 -> ~Feb 1
#   REV     revision letter
# --------------------------------------------------------------------------
BUILD_RE = re.compile(r"^(?P<letter>[A-Z])(?P<yy>\d{2})(?P<ddd>\d{3})(?P<rev>[A-Z])$")

# Firmware builds we have real evidence for. `line` is the HMI / software line;
# None means "we do not know it" -- which is NOT the same as unsupported, it
# means unprovable from the build code alone and therefore DENY for flashing.
#
# NOTE ON LETTERS: build letters are NOT monotonic with the software line.
# P and Q are 006_xxx while S, U, V, X and Y are all 007_xxx, and the Y branch
# alone spans 007_039 through 007_051. So a letter on its own proves almost
# nothing -- only the full build code (or the line itself) decides.
#
FIRMWARE_DB: dict[str, dict] = {
    # --- iDrive 4 era (001_xxx). The flasher names these as unsafe.
    "E16382A": {"line": "001_056_001", "istep": "ID4"},
    "E17235A": {"line": "001_060_001", "istep": "ID4"},
    # --- 005_xxx
    "H16205L": {"line": None, "istep": "16.07"},
    "H16261I": {"line": None, "istep": "16.07"},
    "J16383I": {"line": "005_002_016", "istep": "16.11"},
    "K17052I": {"line": "005_012_001", "istep": "17.03"},
    # --- 006_xxx
    "N18032I": {"line": "006_016_002", "istep": "18.03"},
    "N18092I": {"line": "006_017_006", "istep": "18.03"},
    "P18362I": {"line": "006_027_001", "istep": "18.11"},
    "Q18491I": {"line": "006_032_012", "istep": "18.11"},
    # --- 007_xxx  <- the family the OPT_DEBUG container fits
    "S19221I": {"line": "007_001_016", "istep": "19.11"},
    "U19384I": {"line": "007_007_008", "istep": "19.11"},
    "V20153I": {"line": "007_020_010", "istep": "20.07"},
    "X20511I": {"line": "007_030_001", "istep": "21.03"},
    "Y21432J": {"line": "007_039_008", "istep": "21.11"},
    "Y22292I": {"line": "007_047_001", "istep": "22.11"},
    "Y23236I": {"line": "007_049_001", "istep": "22.11"},
    "Y24073J": {"line": "007_051_002", "istep": "24.07",
                "note": "This is the flasher's own REFERENCE BUILD (007.051.002)."},
}
for _b, _d in FIRMWARE_DB.items():
    _d.setdefault("source", "published version table")

# The OPT_DEBUG container is byte-identical across the whole 007.xxx family.
# Verified members, per the flasher header.
# The flasher header's shorthand "007_001_016 / 030 / 039 / 049 / 051" names the
# MIDDLE group, i.e. these five real builds:
OPTDEBUG_VERIFIED_LINES = {
    "007_001_016",   # S19221I
    "007_030_001",   # X20511I
    "007_039_008",   # Y21432J
    "007_049_001",   # Y23236I
    "007_051_002",   # Y24073J  <- the reference build itself
}
OPTDEBUG_REFERENCE = "007.051.002"

# Firmware builds the gauge-app installer has known-good framework hashes for.
# Kept in sync with tools/apps/app_installer.py PROFILES.
APPS_PROFILE_BUILDS = {"N18032I", "N18092I", "P18362I", "V20153I", "Y24073J"}

# A-F are iDrive 4 -- the one case a bare letter is decisive, and it only DENIES.
# There is deliberately no "too new" letter set: letters do not order by line
# (see the note on FIRMWARE_DB), and an earlier version of this gate wrongly
# blocked S/U/V/X/Y -- which are exactly the 007_xxx family that IS supported.
IDRIVE4_LETTERS = set("ABCDEF")


def builds_for_letter(letter: str | None) -> list[str]:
    """Every known build code on a given branch letter."""
    return sorted(b for b in FIRMWARE_DB if letter and b.startswith(letter))


def parse_build(raw: str | None) -> dict:
    """Parse 'NBTevo_Y24073J' / 'Y24073J' into its parts. Never raises."""
    out = {"raw": raw, "build": None, "letter": None, "year": None, "day": None, "rev": None}
    if not raw or not isinstance(raw, str):
        return out
    s = raw.strip().upper()
    s = re.sub(r"^NBT\s*EVO[_\s-]*", "", s)
    s = re.sub(r"^NBTEVO[_\s-]*", "", s)
    s = s.strip().strip("_-")
    m = BUILD_RE.match(s)
    if not m:
        # still surface a bare leading letter, e.g. the operator typed just "Y"
        if len(s) == 1 and s.isalpha():
            out["letter"] = s
        return out
    out["build"] = s
    out["letter"] = m.group("letter")
    out["year"] = 2000 + int(m.group("yy"))
    out["day"] = int(m.group("ddd"))
    out["rev"] = m.group("rev")
    return out


def infer_platform(firmware: str | None) -> str | None:
    """'NBTevo_Y24073J' -> 'nbt_evo'. Mirrors hu_platform.classify()."""
    if not firmware or not isinstance(firmware, str):
        return None
    f = firmware.upper()
    if "NBTEVO" in f or "NBT_EVO" in f or "HU_NBT" in f:
        return "nbt_evo"
    if "ENTRYEVO" in f or "ENTRYNAV" in f or "HU_ENTRY" in f:
        return "entrynav2"
    return None


def normalize_line(line: str | None) -> str | None:
    """'007.051.002' and '007_051_002' are the same line."""
    if not line or not isinstance(line, str):
        return None
    s = re.sub(r"[.\s]", "_", line.strip())
    return s.upper() if re.match(r"^\d{3}_\d{2,3}(_\d{3})?$", s) else None


def line_family(line: str | None) -> str | None:
    """'007_051_002' -> '007'."""
    n = normalize_line(line)
    return n.split("_")[0] if n else None


def resolve_line(build: str | None, line: str | None) -> tuple[str | None, str]:
    """Work out the software line from an explicit value, else the build DB."""
    explicit = normalize_line(line)
    if explicit:
        return explicit, "supplied by operator"
    if build and build in FIRMWARE_DB:
        known = normalize_line(FIRMWARE_DB[build].get("line"))
        if known:
            return known, "known for build %s (%s)" % (build, FIRMWARE_DB[build].get("source", "table"))
    return None, "not determined"


# --------------------------------------------------------------------------
# Style policies
# --------------------------------------------------------------------------
def _gate_optdebug(ctx: dict) -> dict:
    """NBT Evo SSH unlock via the OPT_DEBUG starter.cfg flash. 007.xxx ONLY."""
    build, letter, line = ctx["build"], ctx["letter"], ctx["line"]

    if ctx["platform"] and ctx["platform"] != "nbt_evo":
        return _deny("WRONG_PLATFORM",
                     "This is the NBT Evo flasher; the connected unit looks like %s." % ctx["platform"],
                     "Use the add-ons / tooling for that platform instead.")

    if letter and letter in IDRIVE4_LETTERS:
        return _deny("IDRIVE4",
                     "Build letter %s is an iDrive 4 unit (001_xxx line)." % letter,
                     "The flasher header names 001_05x explicitly as a DO-NOT-FLASH line. "
                     "Upgrade the unit to ID5/ID6 first.")

    if line:
        fam = line_family(line)
        if fam == "007":
            verified = line in OPTDEBUG_VERIFIED_LINES
            return {
                "Decision": "ALLOW",
                "Allowed": True,
                "Code": "LINE_007_VERIFIED" if verified else "LINE_007_FAMILY",
                "Reason": "Software line %s is in the 007.xxx family%s."
                          % (line, "" if verified else " (family match, not one of the individually verified builds)"),
                "NextStep": "Safe to proceed." if verified else
                            "Proceed with care: the container is byte-identical across 007.xxx, "
                            "but this exact build was not on the verified list.",
            }
        return _deny("LINE_NOT_007",
                     "Software line %s is family %s; this flash fits only 007.xxx." % (line, fam),
                     "Do not flash. A wrong-version starter.cfg fails the next boot.")

    # Only a bare letter, or a build code we have never seen.
    hint = ""
    if letter:
        known = builds_for_letter(letter)
        if known:
            fams = {line_family(FIRMWARE_DB[b]["line"]) for b in known if FIRMWARE_DB[b]["line"]}
            if fams == {"007"}:
                hint = (" Every known %s build is in the 007.xxx family (%s), so this unit is "
                        "probably supported -- but the exact build decides, and letters do not "
                        "order by line." % (letter, ", ".join(known)))
            elif fams:
                hint = (" Known %s builds span %s, so the letter alone cannot decide."
                        % (letter, "/".join(sorted(f + "_xxx" for f in fams))))
    return _deny("UNKNOWN_LINE",
                 "Could not prove the unit's software line%s.%s"
                 % (" (build %s is not in our table)" % build if build else "", hint),
                 "Give the full build string from the car's screen (--firmware NBTevo_Y24073J), "
                 "or the software line from E-Sys/ISTA (--line 007_051_002). The gate refuses "
                 "anything it cannot prove.")


def _gate_entrynav2(ctx: dict) -> dict:
    """EntryNav2 roots differently (root password + remount /), not via OPT_DEBUG."""
    if ctx["platform"] and ctx["platform"] != "entrynav2":
        return _deny("WRONG_PLATFORM",
                     "This style is for EntryNav2; the connected unit looks like %s." % ctx["platform"],
                     "Use the NBT Evo unlock for an NBT Evo unit.")
    return {
        "Decision": "ALLOW",
        "Allowed": True,
        "Code": "ENTRYNAV2",
        "Reason": "EntryNav2 roots via its root password and remounts /, not via the OPT_DEBUG "
                  "starter.cfg flash -- so the 007.xxx constraint does not apply.",
        "NextStep": "No firmware-line gate needed for this path (nothing is flashed).",
    }


def _gate_apps(ctx: dict) -> dict:
    """Gauge-app install over SSH. Needs an ALREADY-ROOTED unit."""
    build = ctx["build"]
    if build and build in APPS_PROFILE_BUILDS:
        return {
            "Decision": "ALLOW",
            "Allowed": True,
            "Code": "PROFILE_MATCH",
            "Reason": "Build %s has known-good framework hashes in the installer profiles." % build,
            "NextStep": "Requires SSH to already be open on the unit. This does NOT mean the "
                        "OPT_DEBUG unlock can root it -- check the 'nbt_optdebug' style separately.",
        }
    return _deny("NO_PROFILE",
                 "Build %s is not in the installer's known-good profiles." % (build or "?"),
                 "The installer verifies framework hashes at install time and will refuse a mismatch.")


def _deny(code: str, reason: str, nextstep: str) -> dict:
    return {"Decision": "DENY", "Allowed": False, "Code": code, "Reason": reason, "NextStep": nextstep}


STYLES = {
    "nbt_optdebug": {
        "label": "NBT Evo SSH unlock (OPT_DEBUG starter.cfg flash)",
        "platform": "nbt_evo",
        "writes_flash": True,
        "supported": "NBT Evo software line 007.xxx only (reference %s)" % OPTDEBUG_REFERENCE,
        "fn": _gate_optdebug,
    },
    "entrynav2_root": {
        "label": "EntryNav2 root (root password + remount)",
        "platform": "entrynav2",
        "writes_flash": False,
        "supported": "EntryNav2 / EntryEVO units; no firmware-line constraint",
        "fn": _gate_entrynav2,
    },
    "apps_install": {
        "label": "Gauge-app install over SSH (already-rooted unit)",
        "platform": "nbt_evo",
        "writes_flash": False,
        "supported": "builds with known-good framework hashes: " + ", ".join(sorted(APPS_PROFILE_BUILDS)),
        "fn": _gate_apps,
    },
}


def check(style: str, firmware: str | None = None, line: str | None = None,
          platform: str | None = None) -> dict:
    """Decide whether `style` may run against this unit.

    firmware -- the string off the car's screen, e.g. 'NBTevo_Y24073J'
                (Navigation -> Options -> Settings -> Position & Version)
    line     -- the software line from E-Sys / ISTA, e.g. '007_051_002'. Authoritative.
    platform -- 'nbt_evo' | 'entrynav2' if already detected.
    """
    spec = STYLES.get(style)
    if not spec:
        return {"Decision": "DENY", "Allowed": False, "Code": "UNKNOWN_STYLE",
                "Reason": "No such style: %s" % style,
                "NextStep": "Known styles: " + ", ".join(sorted(STYLES))}

    parsed = parse_build(firmware)
    resolved, how = resolve_line(parsed["build"], line)
    ctx = {
        "build": parsed["build"],
        "letter": parsed["letter"],
        "line": resolved,
        # The firmware string itself identifies the unit: "NBTevo_..." is an
        # NBT Evo, "EntryEVO/EntryNav..." is an EntryNav2. An explicitly passed
        # platform still wins (it came from a live detect).
        "platform": platform or infer_platform(firmware),
    }

    verdict = spec["fn"](ctx)
    verdict.update({
        "Style": style,
        "StyleLabel": spec["label"],
        "WritesFlash": spec["writes_flash"],
        "Supported": spec["supported"],
        "Firmware": parsed["raw"],
        "Build": parsed["build"],
        "Letter": parsed["letter"],
        "BuildYear": parsed["year"],
        "Line": resolved,
        "LineSource": how,
    })
    return verdict


def matrix(firmware: str | None = None, line: str | None = None,
           platform: str | None = None) -> dict:
    """Run every style against one unit -- the 'which styles work here' answer."""
    return {s: check(s, firmware=firmware, line=line, platform=platform) for s in STYLES}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Pre-flight compatibility gate for the SSH / install styles.")
    ap.add_argument("--firmware", help="build string off the car's screen, e.g. NBTevo_Y24073J")
    ap.add_argument("--line", help="software line from E-Sys/ISTA, e.g. 007_051_002 (authoritative)")
    ap.add_argument("--platform", choices=["nbt_evo", "entrynav2"])
    ap.add_argument("--style", help="check one style only")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)

    if a.style:
        out = check(a.style, a.firmware, a.line, a.platform)
        if a.json:
            print(json.dumps(out, indent=2))
        else:
            print("%s\n  %s -- %s\n  %s" % (out["StyleLabel"], out["Decision"], out["Reason"], out["NextStep"]))
        return 0 if out["Allowed"] else 1

    out = matrix(a.firmware, a.line, a.platform)
    if a.json:
        print(json.dumps(out, indent=2))
        return 0

    head = a.firmware or "(no firmware given)"
    print("Unit: %s    line: %s" % (head, out["nbt_optdebug"]["Line"] or "unknown"))
    print("=" * 72)
    for s, v in out.items():
        mark = "ALLOW" if v["Allowed"] else "DENY "
        flash = "  [WRITES FLASH]" if v["WritesFlash"] else ""
        print("\n[%s] %s%s" % (mark, v["StyleLabel"], flash))
        print("       %s" % v["Reason"])
        print("       -> %s" % v["NextStep"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
