# PyInstaller spec for the TunedByWali NBT Evo Toolbox.
#   pyinstaller packaging/toolbox.spec
# Produces one standalone executable (dist/TunedByWali-NBT-Toolbox[.exe]) that
# bundles Python + the UI + the whole backend + the app packages + the toolkit.
# End users need no Python. Built by build_windows.bat and by CI.
import os

ROOT = os.path.abspath(os.getcwd())
TOOLS = os.path.join(ROOT, "tools")
TOOLKIT = os.path.join(ROOT, "third_party", "nbt-evo-toolkit")

# Everything the running app reads from disk, kept at the same relative paths.
datas = [
    (os.path.join(ROOT, "source", "index.html"), "source"),
    (TOOLS, "tools"),
    (os.path.join(ROOT, "packages"), "packages"),
    (TOOLKIT, "third_party/nbt-evo-toolkit"),
]

# Our backend modules are imported at runtime after sys.path is extended; list
# them so PyInstaller also freezes them as real modules (belt and suspenders -
# they're in datas too, so either path works). The nbt-evo-toolkit is run as a
# subprocess (--run-tool) against its files in datas, so it needs no import here.
hidden = ["toolbox", "enet", "hu_platform", "carplay_fsc", "addon_kit",
          "toolkit_bridge", "app_installer"]

a = Analysis(
    ["app.py"],
    pathex=[
        ROOT, TOOLS,
        os.path.join(TOOLS, "addons"),
        os.path.join(TOOLS, "coding"),
        os.path.join(TOOLS, "apps"),
    ],
    binaries=[],
    datas=datas,
    hiddenimports=hidden,
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "pytest", "numpy", "PIL", "pandas", "matplotlib"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz, a.scripts, a.binaries, a.datas, [],
    name="TunedByWali-NBT-Toolbox",
    console=True,            # small status window; closing it stops the toolbox
    icon=None,
    onefile=True,
)
