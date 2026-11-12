# Add-ons

The toolbox can install community head-unit add-ons over the root SSH it enables —
reusing one framework (`tools/addons/addon_kit.py`, registry `tools/addons/addons.json`).
It adapts each project's USB install to a direct SSH push (no USB stick), always backing up
on-device first and recording a removal marker. **No third-party binaries ship in this repo**
— you stage each add-on from its own release into a *kit* folder. Credits/licenses: `NOTICES.md`.

## How an install runs

`addon-install {id, kit, Head}` →
1. validate the kit (expected files present),
2. **platform guard** — identify the *connected* unit from the car itself and refuse if it
   doesn't match the add-on's `platform` (an NBT-Evo program can't land on an EntryNav2, or
   vice versa). UI filtering by platform is the first line; this server-side guard is the one
   that actually protects the unit.
3. **prerequisite check** — run the add-on's `verify` (e.g. Android Auto's state file must
   exist before the tile or iDrive Plus installs); refuse if missing.
4. remount the HU filesystem read-write,
5. back up each `backup_paths` entry to `/var/opt/tbw-addons/<id>.<timestamp>/`,
6. upload the kit (whole folder, or listed files),
7. run the add-on's verified `steps`,
8. write an install marker. Any failed step stops and points at the backup.

Each add-on declares `platform` (`nbt_evo` / `entrynav2`), and dependent add-ons declare a
`verify` command + `verify_hint`. Both guards run before anything is written.

`addon-remove` runs the add-on's `remove_steps` (restoring hooked files); `addon-status`
reports installed/not. In the wizard: **Add-ons → Show available add-ons**, then Install /
Remove / Status per card (enabled once SSH is on).

## The add-ons

| id | What | License | Needs |
|---|---|---|---|
| `nbt-aad` | **Android Auto** (the base) | PolyForm NC | — |
| `aa-menu-tile` | AA tile in the main menu | PolyForm NC | `nbt-aad` |
| `idrive-plus-plugins` | Weather/news/fuel/traffic pages | PolyForm NC | `nbt-aad` |
| `entrynav2-aa` | Android Auto for EntryNav2 | GPL-3.0 | usb-device.conf |

**Android Auto (NBT-AAD) is the foundation** — the tile and iDrive Plus both require it.
Install `nbt-aad` first.

### Staging a kit
Download the add-on's release (link in each card / `source` in the manifest), unzip it, and
point the card's **Kit folder** at the unzipped folder. For `aa-menu-tile` the kit is the
`nbt-aa-tile` release folder (`aa-tile`, `*.sh`, …); for `idrive-plus-plugins` it's the
`chiavetta` install folder; for `entrynav2-aa` it's `runtime/aa` plus a filled-in
`usb-device.conf` (your phone's USB port + VID/PID — see that project's README).

### Marked "needs verification"
`nbt-aad` and `entrynav2-aa` ship here with **no install steps** on purpose — their flows
need confirming on a reference car (and EntryNav2 needs the per-phone VID/PID). Until you add
verified `steps` to `addons.json`, Install returns "pending" and writes nothing. `aa-menu-tile`
and `idrive-plus-plugins` carry their published steps, but still test on a reference car first.

## Not part of the SSH flow

- **iDrive Plus phone app** — an Android APK (sideloaded from its release). The toolbox installs
  the *HU plugins*; the phone app is separate. News/fuel/traffic data is Italy-only as shipped.
- **Remote-start FSC** — a different ECU (DME/BDC3, mainly G-series), applied with **E-Sys**, not
  the NBT Evo SSH. It's referenced only (`NOTICES.md`), experimental, and unlicensed — use the
  author's generator directly and install the resulting `.fsc` with E-Sys. It shares the
  low-exponent self-signed FSC approach used elsewhere, so it can inform the CarPlay FSC work
  (`docs/CARPLAY_ACTIVATE_FSC.md`).
