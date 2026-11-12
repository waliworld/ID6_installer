# Platforms & capability gating

The toolbox identifies the connected head unit first, then only offers what that unit
supports. Detection (`platform-detect`, `tools/hu_platform.py`) classifies from the firmware
string (read over the diagnostic bus / SSH, or typed): `NBTevo_*` → **NBT Evo**;
`EntryEVO` / `EntryNav` / `HU_ENTRY` → **EntryNav2**; otherwise **unknown**.

The wizard shows a platform banner and hides the sections that don't apply.

| | NBT Evo (HU_NBT2) | EntryNav2 (HU_ENTRY / EntryEVO) |
|---|---|---|
| SSH root | **OPT_DEBUG flash** (bundled flasher) | **root password** (obtained separately) |
| Gauges | ✅ | — |
| Coding & FSC | ✅ (nbt-evo-toolkit) | — (different keys/containers) |
| CarPlay activate / full-screen | ✅ | — |
| Android Auto add-on | NBT-AAD + AA menu tile | **entry-aad** |

## SSH root *is* OPT_DEBUG (on NBT Evo) — one mechanism, not two

On NBT Evo, "enable SSH" and "OPT_DEBUG" are the **same action**: flipping `OPT_DEBUG`
`STOP→RUN` in `starter.cfg` is what brings up `sshd`/`telnet`/`qconn` as root. The Bavarian SSH
client and the bundled flasher are two *methods* to that one result — the toolbox uses one (the
flasher), not both. **EntryNav2 is a different head unit**: it does not use OPT_DEBUG flashing at
all; entry-aad needs the unit's own root password, obtained another way. That's why the
Enable-SSH / gauges / coding steps are hidden when an EntryNav2 is detected.

## No duplicates

Where two projects do the same job, the toolbox keeps one and credits the other:
- EntryNav2 Android Auto → **entry-aad** (Jiraiya43); kizapp's `entrynav2-idrive6-android-auto`
  (GPL-3.0) is the credited alternative, not a second entry.
- AA main-menu tile → **nbt-aa-tile** (deathwalker80, standalone); Keukenrol123522's in-build
  variant is credited, not duplicated.
- SSH enable → the one OPT_DEBUG flasher (Bavarian is the credited alternative method).

## VIN & PSdZData (coding/FSC)

- **VIN auto-detect:** Identify reads the VIN off the car and fills it in; the Coding & FSC
  section shows it. Confirming "I'm authorized to modify this VIN" sets the toolkit's
  `NBTEVO_OWN_VIN` consent for that car (the toolkit still re-checks it against the connected VIN
  on any write — never bypassed).
- **PSdZData through the system:** `psdzdata-locate` finds your existing E-Sys/ISTA PSdZData in
  the usual install locations (or `NBTEVO_PSDZDATA`) — you don't re-supply it; coding/FSC read it
  from the system directly.
- **Write-proofing:** every coding write backs up the live coding first (`coding-backup`) and
  reads back + diffs after; `coding-set` resolves the token to its coordinate and stages the
  signed-write sequence rather than firing an unverified write.
