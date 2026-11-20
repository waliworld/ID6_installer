# Getting the program to your clients

Your clients should never need to install Python or touch a command line. They
get **one `.exe`** and double-click it. That `.exe` has to be built on Windows
(a Windows program can only be built on Windows). Pick whichever is easier for you:

## Option A — build it on a shop Windows PC (simplest, you control it)
1. On a Windows PC with **Python 3** installed (only needed on this build PC, never
   on clients), download this repository (green **Code** button → **Download ZIP**,
   then unzip).
2. Double-click **`build_windows.bat`**.
3. A few minutes later you get **`dist\TunedByWali-NBT-Toolbox.exe`** — a single file.
4. Send that `.exe` to clients. They double-click it; the toolbox opens in their
   browser. The first time, Windows SmartScreen may warn it's unsigned → **More info
   → Run anyway**. OpenSSH is already in Windows 10/11.

## Option B — build it on GitHub, no PC needed (all in the browser)
1. On this repo's page, open the **Actions** tab.
2. Choose **"Build client program"** on the left → **Run workflow** → **Run workflow**.
3. Wait for it to finish (green check, a few minutes), click into the run, and
   download **`TunedByWali-NBT-Toolbox-windows-x64.exe`** (and the macOS one) from the
   **Artifacts** section at the bottom.
4. Send the `.exe` to clients as in Option A.

*(Publishing a version tag like `v0.3.0` does the same build and also attaches the
files to a Release page. `git tag v0.3.0 && git push origin v0.3.0`.)*

---

## Run it yourself right now (needs Python)
For your own testing on a PC that has Python 3:

- Double-click **`Start Toolbox (Windows).bat`** / **`Start Toolbox (Mac).command`**, or
- `python app.py`

The toolbox opens at a local address. Everything runs on your computer.

## What it does
Connect to the car over the ENET cable, enable SSH, identify the unit, install any of
the apps (BoostDeck, ShiftDeck, MHD+ Suite, Engine Codes, HUD Gauges, Bootmod3, xDrive,
Road Jet, BEAMer), enable/full-screen CarPlay and coding, add Android Auto, and close
SSH back up — all from one window.
