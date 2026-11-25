# Downloads

## `TBW-NBT-Toolbox-v0.4.0-portable.zip` — runnable now
The complete program. Download this one file, unzip it, then:
- **Windows:** double-click `Start Toolbox (Windows).bat`
- **Mac:** double-click `Start Toolbox (Mac).command`
- or run `python app.py`

The toolbox opens in your browser. Needs **Python 3** and **OpenSSH** (OpenSSH is built into
Windows 10/11 and macOS). Rebuild it anytime with `python build.py portable`.

### New in v0.4.0 — read this before unlocking anything

**v0.3.0 has been removed. Do not use an older copy.** It shipped the SSH unlock with no
version check, which is what leaves units on a black screen.

- **Pre-flight compatibility gate.** The unlock now refuses to run unless the unit is
  *positively proven* to be on the supported software line. "Enable SSH" will not arm until
  the compatibility check passes. A unit it cannot verify is refused — by design.
- **Recovery panel.** For a head unit left dark by an aborted flash. **Scan is read-only**
  and writes nothing; it tells you whether the unit is latched in flash mode, already clean,
  or off the bus entirely.

Read [`docs/SSH_STYLES.md`](../docs/SSH_STYLES.md) first. It has the support matrix, how to
read your unit's software version off the car's own screen in about ten seconds, and the
recovery ladder.

The short version: the OPT_DEBUG SSH unlock works on **NBT Evo software line 007.xxx only**.
A firmware being listed as supported for installing gauge apps does **not** mean the unlock
can root it — those are different questions, and the gate answers both.

## The no-Python `.exe` / macOS app
These are built by CI on real Windows/macOS machines — this repository's tooling can't be run
from the environment that commits here, so a **maintainer pushes a version tag** to produce them:

```sh
git tag v0.4.0
git push origin v0.4.0
```

That fires `.github/workflows/release.yml`, which runs `pyinstaller packaging/toolbox.spec` on
Windows and macOS runners and publishes a GitHub Release with:
- `TunedByWali-NBT-Toolbox-windows-x64.exe`
- `TunedByWali-NBT-Toolbox-macos`

Those bundle Python + the UI + backend + `packages/` + the toolkit, so end users need no Python —
just download, double-click (allow past the unsigned-app warning the first time).

To build one locally instead (on the target OS, with PyInstaller): `python build.py exe`.
