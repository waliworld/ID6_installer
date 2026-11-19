# PyInstaller spec for the TunedByWali NBT Evo Toolbox.
# Built by CI on real Windows/macOS runners (see .github/workflows/release.yml):
#     pyinstaller packaging/toolbox.spec
# Produces a single self-contained executable that bundles Python + the UI +
# backend + app packages + the nbt-evo-toolkit. End users need no Python.
import os
from PyInstaller.utils.hooks import collect_submodules

ROOT = os.path.abspath(os.getcwd())

# Everything the running app reads from disk, kept at the same relative paths.
datas = [
    (os.path.join(ROOT, "source", "index.html"), "source"),
    (os.path.join(ROOT, "tools"), "tools"),
    (os.path.join(ROOT, "packages"), "packages"),
    (os.path.join(ROOT, "third_party", "nbt-evo-toolkit"), "third_party/nbt-evo-toolkit"),
]

# The backend imports siblings dynamically and the toolkit is run as modules;
# pull them in so the frozen build can import/run them.
hidden = []
for pkg in ("addons", "coding", "apps", "hu-info", "ssh-unlock"):
    pass  # these are path-imported; their files ride along via datas
hidden += collect_submodules("capabilities")  # nbt-evo-toolkit (best effort)
hidden += ["carplay_fsc", "addon_kit", "toolkit_bridge", "app_installer",
           "hu_platform", "enet", "toolbox"]

a = Analysis(
    ["app.py"],
    pathex=[ROOT, os.path.join(ROOT, "tools"),
            os.path.join(ROOT, "tools", "addons"),
            os.path.join(ROOT, "tools", "coding"),
            os.path.join(ROOT, "tools", "apps"),
            os.path.join(ROOT, "third_party", "nbt-evo-toolkit")],
    binaries=[],
    datas=datas,
    hiddenimports=hidden,
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "pytest", "numpy", "PIL"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz, a.scripts, a.binaries, a.datas, [],
    name="TunedByWali-NBT-Toolbox",
    console=True,            # a small status window; closing it stops the app
    disable_windowed_traceback=False,
    icon=None,
    onefile=True,
)
