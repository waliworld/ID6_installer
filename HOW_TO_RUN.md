# How to run the toolbox

## Easiest — the double-click program (no Python needed)
1. Open this repo's **Releases** page and download the file for your computer:
   - **Windows:** `TunedByWali-NBT-Toolbox-windows-x64.exe`
   - **Mac:** `TunedByWali-NBT-Toolbox-macos`
2. Double-click it. A small status window opens and the toolbox appears in your browser.
   - Windows may warn it's unsigned (SmartScreen): **More info → Run anyway**.
   - Mac: the first time, **right-click → Open**.
3. Close the status window (or press Ctrl+C) to stop.

Releases are built automatically when a version tag is pushed (`.github/workflows/release.yml`),
so each release has fresh Windows and Mac downloads.

## From source (if you have Python 3)
Double-click **`Start Toolbox (Windows).bat`** or **`Start Toolbox (Mac).command`**,
or run:

```sh
python app.py
```

The toolbox opens at a local address (e.g. http://127.0.0.1:xxxxx). Everything runs on your
computer. **OpenSSH** is the only system requirement and ships with Windows 10/11 and macOS.

## What it does
Connect to the car over the ENET cable, enable SSH, identify the unit, install any of the apps
(BoostDeck, ShiftDeck, MHD+ Suite, Engine Codes, HUD Gauges, Bootmod3, xDrive, Road Jet, BEAMer),
turn on CarPlay / coding, add Android Auto, and close SSH back up — all from one window.
