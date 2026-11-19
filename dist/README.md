# Downloads

## `TBW-NBT-Toolbox-v0.3.0-portable.zip` — runnable now
The complete program. Download this one file, unzip it, then:
- **Windows:** double-click `Start Toolbox (Windows).bat`
- **Mac:** double-click `Start Toolbox (Mac).command`
- or run `python app.py`

The toolbox opens in your browser. Needs **Python 3** and **OpenSSH** (OpenSSH is built into
Windows 10/11 and macOS). Rebuild it anytime with `python build.py portable`.

## The no-Python `.exe` / macOS app
These are built by CI on real Windows/macOS machines — this repository's tooling can't be run
from the environment that commits here, so a **maintainer pushes a version tag** to produce them:

```sh
git tag v0.3.0
git push origin v0.3.0
```

That fires `.github/workflows/release.yml`, which runs `pyinstaller packaging/toolbox.spec` on
Windows and macOS runners and publishes a GitHub Release with:
- `TunedByWali-NBT-Toolbox-windows-x64.exe`
- `TunedByWali-NBT-Toolbox-macos`

Those bundle Python + the UI + backend + `packages/` + the toolkit, so end users need no Python —
just download, double-click (allow past the unsigned-app warning the first time).

To build one locally instead (on the target OS, with PyInstaller): `python build.py exe`.
