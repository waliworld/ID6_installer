#!/usr/bin/env python3
"""
app_installer.py - the single ID6Apps installer for the TBW toolbox.

One installer for every app, taken straight from the packages vendored in this
repo (packages/*.zip) - nothing to download or stage. Covers what the upstream
desktop installer does:

  * app catalog discovered from the packages (9 apps today, new ones auto-appear)
  * firmware profile matching (framework SHA-256 pairs, COMPATIBILITY.md)
  * display-aware layout generation for 800x480, 960x480, 1280x480, 1440x540,
    1920x720 and validated custom sizes (layout logic ported from display.go)
  * select any subset to INSTALL or UNINSTALL
  * on-device backup before writing, upload verified by hash read-back, HMI reset

Safety: reuses the toolbox's platform guard (an NBT Evo package can't land on an
EntryNav2) and refuses unless the ID5 Plugins directory is present. Reads are
free; nothing is written until the guards pass and a backup exists.
"""
from __future__ import annotations
import hashlib
import json
import re
import shlex
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PACKAGES = REPO / "packages"
PLUGINS = "/var/opt/hmi/data/ID5/Plugins"
BACKUPS = "/var/opt/tbw-addons"

sys.path.insert(0, str(HERE.parent / "addons"))
import addon_kit as ak  # shared ssh/put/platform helpers - one implementation

# ---- firmware profiles: installer-eligible pairs (source/docs/COMPATIBILITY.md)
PROFILES = {
    "N18032I": ("93f4c7ab7e59c0adf7923b9b491de06165ba9d97bbe7412292e7ddc418ca2123",
                "64ba154c68c772f9ca779fc18507f5b34715be58ded4a198b8a62b90d16c3ee0"),
    "N18092I": ("93f4c7ab7e59c0adf7923b9b491de06165ba9d97bbe7412292e7ddc418ca2123",
                "64ba154c68c772f9ca779fc18507f5b34715be58ded4a198b8a62b90d16c3ee0"),
    "P18362I": ("d9c674c210272e8f07473e73e59ed8f23d984d4d30162971bedb56d21c682b28",
                "896d9de22fa85f7f5aa4fc2a0e62003c63c41c47186b06f39a2fd4fee06fa5d5"),
    "V20153I": ("085b7f2a9f96734be51621a70a04c24d01525eb5e9852f45c845a7d2f4ae4c67",
                "56705e56c349d9ae57a7782bd39b795cfc01391ba8ff155046f32fbe8ffad2cb"),
    "Y24073J": ("ef2488b007d592301d8f21d9aa60771ad552d06b5e97c29e7813e3946644d820",
                "cb0e488d1ebe2e30de87bb56f0e93d6a2d76b44309b0b47fef5b0b7cec048973"),
}
APP_FW = "/opt/hmi/ID5/lib/libAppFramework.A.so"
CORE_FW = "/opt/hmi/ID5/lib/libCoreFramework.A.so"
VERSION_FILE = "/opt/sys/etc/nbt_version.txt"
LM_SCREEN = "/opt/sys/bin/LayerManagerControl get screen 0"

PRESET_DISPLAYS = ["800x480", "960x480", "1280x480", "1440x540", "1920x720"]
# Apps whose packaged layout is 1280x480-only (bitmap canvas, no reflow).
WIDE_ONLY = {"RoadJet", "BEAMer"}
NOTES = {
    "Bootmod3Suite": "beta - CustomROM tunes; burble presets need HSFZ",
    "XDriveControl": "experimental - DSC-dependent, needs HSFZ",
    "HudTest": "targets the reference 340i factory HUD",
    "RoadJet": "requires a 1280x480 display",
    "BEAMer": "requires a 1280x480 display",
}

_PROP = re.compile(rb'<property id="(9|10|20|21)" value="([0-9]+)"')


# ------------------------------------------------------------------ catalog
def catalog(_body=None) -> dict:
    """Discover every installable app from the vendored packages."""
    apps = []
    for z in sorted(PACKAGES.glob("*.zip")):
        try:
            with zipfile.ZipFile(z) as zf:
                names = zf.namelist()
        except (OSError, zipfile.BadZipFile):
            continue
        tops = {n.split("/")[0] for n in names if "/" in n}
        for d in sorted(tops):
            if f"{d}/{d}.manifest" not in names:
                continue  # not an app folder (source/, docs, ...)
            files = [n for n in names if n.startswith(d + "/") and not n.endswith("/")]
            apps.append({
                "app": d, "package": z.name, "files": sorted(files),
                "wide_only": d in WIDE_ONLY, "note": NOTES.get(d, ""),
            })
    return {"Ok": bool(apps), "Apps": apps, "Displays": PRESET_DISPLAYS,
            "Result": f"{len(apps)} apps available from {len(set(a['package'] for a in apps))} packages."}


# ------------------------------------------------------- layout generation
def parse_display(value: str) -> tuple[int, int] | None:
    m = re.fullmatch(r"\s*(\d{3,4})\s*[xX×]\s*(\d{3,4})\s*", value or "")
    if not m:
        return None
    w, h = int(m.group(1)), int(m.group(2))
    # same envelope the engine enforces: 800-1920 wide, 480-720 high, 5:3..3:1
    if not (800 <= w <= 1920 and 480 <= h <= 720 and w * 3 >= h * 5 and w <= h * 3):
        return None
    return w, h


def scale_layout(xml: bytes, app: str, w: int, h: int) -> bytes:
    """Port of display.go scaleLayout: the packaged XML uses a 1280x480 canvas.
    Scale only explicit geometry (property ids 9/10 = size, 20/21 = position)."""
    if (w, h) == (1280, 480):
        return xml
    x_scale, y_scale = w, h
    if app in ("ShiftDeck", "MhdMaps"):
        # fit the bitmap canvas inside the panel without stretching it
        x_scale = min(x_scale, h * 1280 // 480)
        y_scale = min(y_scale, w * 480 // 1280)

    def repl(mo):
        pid = mo.group(1).decode()
        val = int(mo.group(2))
        if pid in ("9", "20"):
            val = (val * x_scale + 640) // 1280
            if pid == "20":
                val += (w - x_scale) // 2
        else:
            val = (val * y_scale + 240) // 480
            if pid == "21":
                val += (h - y_scale) // 2
        return b'<property id="%s" value="%d"' % (pid.encode(), val)

    return _PROP.sub(repl, xml)


# -------------------------------------------------------------- compat read
def compat(body: dict) -> dict:
    head, key = body.get("Head") or body.get("head"), body.get("Key") or body.get("key")
    if not head:
        return {"Ok": False, "Result": "Head unit IP required (enable SSH first)."}
    if not ak._reach(head, key):
        return {"Ok": False, "Result": f"Could not reach root@{head}. Enable SSH first."}
    plat, raw = ak._hu_platform(head, key)
    fw = ak._ssh(head, key, f"cat {VERSION_FILE} 2>/dev/null")[1] or raw
    disp = ak._ssh(head, key, LM_SCREEN)[1]
    hashes = {}
    for label, path in (("app", APP_FW), ("core", CORE_FW)):
        out = ak._ssh(head, key, f"sha256sum {shlex.quote(path)} 2>/dev/null || shasum -a 256 {shlex.quote(path)}")[1]
        hashes[label] = out.split()[0] if out else None
    name = None
    m = re.search(r"NBTevo_([A-Za-z][0-9]{5}[A-Za-z])", fw or "")
    if m:
        name = m.group(1).upper()
    matched = bool(name and PROFILES.get(name) == (hashes["app"], hashes["core"]))
    panel = None
    dm = re.search(r"resolution:\s*x\s*=\s*(\d{2,4}),\s*y\s*=\s*(\d{2,4})", disp or "")
    if dm:
        panel = f"{dm.group(1)}x{dm.group(2)}"
    writable = ak._ssh(head, key, f"test -d {PLUGINS} && touch {PLUGINS}/.tbw && rm -f {PLUGINS}/.tbw")[0] == 0
    ok = plat == "nbt_evo" and matched and writable
    return {"Ok": ok, "Platform": plat, "Firmware": name or (fw or None), "Display": panel,
            "FrameworkHashes": hashes, "ProfileMatched": matched, "PluginsWritable": writable,
            "Result": ("Ready: " + (name or "?") + " matched, display " + (panel or "unknown") + ".")
            if ok else ("Not eligible — " + ("not an NBT Evo. " if plat != "nbt_evo" else "")
                        + ("firmware/framework pair is not a known-good profile. " if not matched else "")
                        + ("ID5 Plugins directory is not writable. " if not writable else ""))}


# ----------------------------------------------------------- install/remove
def _extract(app: dict, dest: Path) -> list[Path]:
    out = []
    with zipfile.ZipFile(PACKAGES / app["package"]) as zf:
        for n in app["files"]:
            t = dest / Path(n).name
            t.write_bytes(zf.read(n))
            out.append(t)
    return out


def _selected(body: dict) -> tuple[list[dict], list[str]]:
    want = body.get("apps") or body.get("Apps") or []
    if isinstance(want, str):
        want = [w.strip() for w in want.split(",") if w.strip()]
    known = {a["app"]: a for a in catalog()["Apps"]}
    return [known[w] for w in want if w in known], [w for w in want if w not in known]


def install(body: dict) -> dict:
    head, key = body.get("Head") or body.get("head"), body.get("Key") or body.get("key")
    chosen, unknown = _selected(body)
    if not chosen:
        return {"Ok": False, "Result": "Select at least one app to install." +
                (f" Unknown: {', '.join(unknown)}" if unknown else "")}
    if not head:
        return {"Ok": False, "Result": "Head unit IP required (enable SSH first)."}

    c = compat({"Head": head, "Key": key})
    if not c.get("Ok") and not body.get("force"):
        return {"Ok": False, "Blocked": True, "Compat": c,
                "Result": "Refusing to install: " + c.get("Result", "compatibility check failed") +
                          " Nothing was written."}

    disp = body.get("display") or c.get("Display") or "1280x480"
    wh = parse_display(disp)
    if not wh:
        return {"Ok": False, "Result": f"Unsupported display '{disp}'. Use one of {', '.join(PRESET_DISPLAYS)} "
                                        "or a size 800-1920 x 480-720 within a 5:3-3:1 aspect."}
    w, h = wh
    bad = [a["app"] for a in chosen if a["wide_only"] and (w, h) != (1280, 480)]
    if bad:
        return {"Ok": False, "Result": f"{', '.join(bad)} ship a 1280x480-only layout; this display is {w}x{h}. "
                                        "Deselect them or install on a 1280x480 unit."}

    stamp = ak._ssh(head, key, "date +%Y%m%d-%H%M%S")[1] or "backup"
    bdir = f"{BACKUPS}/apps.{stamp}"
    ak._ssh(head, key, f"mkdir -p {shlex.quote(bdir)}")
    log, installed = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for app in chosen:
            name = app["app"]
            d = Path(tmp) / name
            d.mkdir(parents=True, exist_ok=True)
            files = _extract(app, d)
            # display-aware layout
            xml = d / f"{name}.xml"
            if xml.exists():
                xml.write_bytes(scale_layout(xml.read_bytes(), name, w, h))
            # back up an existing copy, then publish
            ak._ssh(head, key, f"[ -d {PLUGINS}/{name} ] && cp -a {PLUGINS}/{name} {shlex.quote(bdir)}/ || true")
            ak._ssh(head, key, f"rm -rf {PLUGINS}/{name}.new && mkdir -p {PLUGINS}/{name}.new")
            for f in files:
                if ak._put(head, key, f, f"{PLUGINS}/{name}.new/{f.name}") != 0:
                    ak._ssh(head, key, f"rm -rf {PLUGINS}/{name}.new")
                    return {"Ok": False, "Backup": bdir,
                            "Result": f"Upload of {name}/{f.name} failed; {name} was not changed."}
            # verify every uploaded file by hash before swapping it in
            for f in files:
                local = hashlib.sha256(f.read_bytes()).hexdigest()
                got = ak._ssh(head, key, f"sha256sum {PLUGINS}/{name}.new/{f.name} 2>/dev/null || "
                                          f"shasum -a 256 {PLUGINS}/{name}.new/{f.name}")[1]
                if not got or got.split()[0] != local:
                    ak._ssh(head, key, f"rm -rf {PLUGINS}/{name}.new")
                    return {"Ok": False, "Backup": bdir,
                            "Result": f"Read-back mismatch on {name}/{f.name}; {name} was not changed."}
            rc = ak._ssh(head, key, f"rm -rf {PLUGINS}/{name} && mv {PLUGINS}/{name}.new {PLUGINS}/{name}")[0]
            if rc != 0:
                return {"Ok": False, "Backup": bdir, "Result": f"Could not publish {name}; backup at {bdir}."}
            installed.append(name)
            log.append(f"{name}: {len(files)} files, layout {w}x{h}")
    ak._ssh(head, key, "OnOffDSICommander appreset")
    return {"Ok": True, "Installed": installed, "Backup": bdir, "Display": f"{w}x{h}", "Log": log,
            "Result": f"Installed {', '.join(installed)} at {w}x{h}. Backup: {bdir}. "
                      "Restart iDrive while parked, then check My Vehicle."}


def uninstall(body: dict) -> dict:
    head, key = body.get("Head") or body.get("head"), body.get("Key") or body.get("key")
    chosen, unknown = _selected(body)
    if not chosen:
        return {"Ok": False, "Result": "Select at least one app to uninstall."}
    if not head or not ak._reach(head, key):
        return {"Ok": False, "Result": f"Could not reach root@{head}. Enable SSH first."}
    plat, _ = ak._hu_platform(head, key)
    if plat != "nbt_evo":
        return {"Ok": False, "Blocked": True, "Result": f"Connected unit is {plat}, not NBT Evo. Nothing changed."}
    stamp = ak._ssh(head, key, "date +%Y%m%d-%H%M%S")[1] or "backup"
    bdir = f"{BACKUPS}/apps-removed.{stamp}"
    ak._ssh(head, key, f"mkdir -p {shlex.quote(bdir)}")
    removed = []
    for app in chosen:
        name = app["app"]
        if ak._ssh(head, key, f"test -d {PLUGINS}/{name}")[0] != 0:
            continue
        ak._ssh(head, key, f"cp -a {PLUGINS}/{name} {shlex.quote(bdir)}/ || true")
        if ak._ssh(head, key, f"rm -rf {PLUGINS}/{name}")[0] == 0:
            removed.append(name)
    ak._ssh(head, key, "OnOffDSICommander appreset")
    return {"Ok": bool(removed), "Removed": removed, "Backup": bdir,
            "Result": (f"Removed {', '.join(removed)}. Backup: {bdir}. Restart iDrive while parked."
                       if removed else "None of the selected apps were installed.")}


if __name__ == "__main__":
    op = sys.argv[1] if len(sys.argv) > 1 else "catalog"
    raw = sys.stdin.read().strip() if not sys.stdin.isatty() else ""
    b = json.loads(raw) if raw else {}
    print(json.dumps({"catalog": catalog, "compat": compat,
                      "install": install, "uninstall": uninstall}[op](b), indent=2))
