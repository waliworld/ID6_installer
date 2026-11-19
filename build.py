#!/usr/bin/env python3
"""
build.py - produce distributables of the NBT Evo Toolbox.

    python build.py portable     # a zip that runs with Python (no build tools)
    python build.py exe          # a standalone .exe/.app (needs PyInstaller)
    python build.py all

'portable' works anywhere and is what dist/ ships. 'exe' needs PyInstaller and
runs on the target OS (build the Windows .exe on Windows, the Mac app on macOS);
CI (.github/workflows/release.yml) does this automatically on a version tag.
"""
from __future__ import annotations
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VERSION = "v0.3.0"
DIST = ROOT / "dist"

# What the running app needs on disk (the Go sources are not used by app.py).
INCLUDE = [
    "app.py", "source/index.html", "tools", "packages", "third_party",
    "Start Toolbox (Windows).bat", "Start Toolbox (Mac).command",
    "HOW_TO_RUN.md", "README.md", "NOTICES.md", "Quick Start.md", "docs",
]
PRUNE_SUFFIXES = (".go",)
PRUNE_NAMES = {"__pycache__", "go.mod", "go.sum"}


def _stage(dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for rel in INCLUDE:
        src = ROOT / rel
        if not src.exists():
            continue
        target = dst / rel
        if src.is_dir():
            shutil.copytree(src, target)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, target)
    # prune files the app doesn't need
    for p in list(dst.rglob("*")):
        if p.name in PRUNE_NAMES or (p.is_file() and p.suffix in PRUNE_SUFFIXES):
            shutil.rmtree(p, ignore_errors=True) if p.is_dir() else p.unlink(missing_ok=True)
    for pyc in dst.rglob("*.pyc"):
        pyc.unlink(missing_ok=True)


def portable() -> Path:
    DIST.mkdir(exist_ok=True)
    stage = DIST / f"TBW-NBT-Toolbox-{VERSION}"
    _stage(stage)
    out = DIST / f"TBW-NBT-Toolbox-{VERSION}-portable.zip"
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(DIST))
    shutil.rmtree(stage)
    print(f"portable: {out} ({out.stat().st_size} bytes)")
    return out


def exe() -> int:
    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("PyInstaller is not installed. Run: python -m pip install pyinstaller",
              file=sys.stderr)
        return 2
    rc = subprocess.call([sys.executable, "-m", "PyInstaller",
                          str(ROOT / "packaging" / "toolbox.spec")], cwd=str(ROOT))
    if rc == 0:
        built = ROOT / "dist" / ("TunedByWali-NBT-Toolbox.exe" if os.name == "nt"
                                 else "TunedByWali-NBT-Toolbox")
        print(f"exe: {built}")
    return rc


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "portable"
    if what in ("portable", "all"):
        portable()
    if what in ("exe", "all"):
        sys.exit(exe())
