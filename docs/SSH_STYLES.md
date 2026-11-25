# Rooting / install styles — which one works on which unit

This toolbox bundles several ways of getting onto a head unit. They are **not
interchangeable**, and the most dangerous mistake is assuming that because one
stage supports a unit, the others do too.

`tools/ssh_gate.py` enforces this. Run it against any car before you connect:

```sh
python tools/ssh_gate.py --firmware NBTevo_Y24073J
```

## The support matrix

| Style | What it does | Writes flash? | Supported units |
|---|---|---|---|
| `nbt_optdebug` | NBT Evo SSH unlock — flips `OPT_DEBUG` in `starter.cfg` | **YES** | NBT Evo **software line 007.xxx only** (reference `007.051.002`) |
| `entrynav2_root` | EntryNav2 root — root password + remount `/` | no | EntryNav2 / EntryEVO. No firmware-line constraint (nothing is flashed) |
| `apps_install` | Gauge-app install over SSH | no | Builds with known-good framework hashes: `N18032I`, `N18092I`, `P18362I`, `V20153I`, `Y24073J` — **and SSH must already be open** |

## The full version table

**Build letters do not order by software line.** P and Q are `006_xxx` while S,
U, V, X and Y are all `007_xxx`, and the Y branch alone spans `007_039` through
`007_051`. A letter on its own proves almost nothing — only the full build code,
or the line itself, decides.

| Build | Software line | I-Step | SSH unlock |
|---|---|---|---|
| `E16382A` | `001_056_001` | ID4 | no — iDrive 4 |
| `E17235A` | `001_060_001` | ID4 | no — iDrive 4 |
| `H16205L` | — | 16.07 | unknown |
| `H16261I` | — | 16.07 | unknown |
| `J16383I` | `005_002_016` | 16.11 | no |
| `K17052I` | `005_012_001` | 17.03 | no |
| `N18032I` | `006_016_002` | 18.03 | no |
| `N18092I` | `006_017_006` | 18.03 | no |
| `P18362I` | `006_027_001` | 18.11 | no |
| `Q18491I` | `006_032_012` | 18.11 | no |
| `S19221I` | `007_001_016` | 19.11 | **yes — verified** |
| `U19384I` | `007_007_008` | 19.11 | yes — family |
| `V20153I` | `007_020_010` | 20.07 | yes — family |
| `X20511I` | `007_030_001` | 21.03 | **yes — verified** |
| `Y21432J` | `007_039_008` | 21.11 | **yes — verified** |
| `Y22292I` | `007_047_001` | 22.11 | yes — family |
| `Y23236I` | `007_049_001` | 22.11 | **yes — verified** |
| `Y24073J` | `007_051_002` | 24.07 | **yes — the reference build** |

*verified* = one of the five builds the flasher header names explicitly
(`007_001_016 / 030 / 039 / 049 / 051` — that shorthand is the **middle** group).
*family* = in `007.xxx`, where the container is byte-identical, but not on that
explicit list.

### Still check each style separately

A unit listed under `apps_install` says nothing on its own about whether
`nbt_optdebug` can root it. `N18092I` and `P18362I` have known-good gauge
profiles but are `006_xxx` and cannot be rooted by this flash. The gate prints
both verdicts so you never have to infer one from the other.

## Why the gate refuses by default

The OPT_DEBUG flash is LIVE-DIRECT: it overwrites the running `starter.cfg`.
Per the flasher's own header, a wrong-version file **fails the next boot** —
that is a brick, not a dark screen.

Upstream (`Xayanix/nbt-evo-ssh-unlocker` issue #3) collected multiple cars that
aborted and were left on a black screen. The maintainer's diagnosis: the tool
opened flash mode, detected a non-007.x version, failed its preconditions, then
**failed to exit flash mode**. Upstream's fix (commit *"Fix exit from flash mode
when preconditions fail"*) improves the exit, but the ordering was still wrong —
flash mode went on *before* the version was known.

**This gate inverts that.** It decides before anything touches EnergyMode:

- a build letter can only ever **DENY**, never ALLOW
- only a confirmed **software line** (e.g. `007_051_002`) can ALLOW
- cannot prove the line → **DENY**, and say how to prove it

So the unlock no longer runs blind. A tech who supplies nothing gets a refusal
with instructions, not a flash attempt.

## Reading the version

**Off the car (10 seconds, no laptop):**
Navigation → Options → Settings → Position & Version → Version information.
You get a string like `NBTevo_Y24073J`.

Build codes decode as `<LETTER><YY><DDD><REV>` — `Y24073J` is branch **Y**,
built day 073 of 2024.

**Authoritative (E-Sys / ISTA):** read the SVK. That gives the software line
itself, which is the only thing the gate will accept as proof:

```sh
python tools/ssh-unlock/nbt_optdebug_flash.py --commit --line 007_051_002
```

Known letter → line mappings are in `FIRMWARE_DB` in `tools/ssh_gate.py`.
Entries whose line is `None` are **unproven, not unsupported** — the gate
refuses them for flashing and says so. Add a line only when you have read it
from a real unit; leave unverified values blank.

## Recovery: dark screen after an aborted flash

`tools/ssh-unlock/nbt_recover.py` — also wired into the app under **Recovery**.

```sh
python tools/ssh-unlock/nbt_recover.py            # READ-ONLY. Do this first.
python tools/ssh-unlock/nbt_recover.py --clear    # clear latched state + reset
```

It writes nothing to the head unit and loads no flash containers. Read-only mode
reports VIN, SVK, EnergyMode, ExtendedMode, the F186 state byte and the fault
memory, then gives a verdict:

- **Latched flash state found** → `--clear` runs the flasher's tested teardown
  (ActivateApplicationMode → clear EnergyMode/ExtendedMode → CommControl normal
  → DTC on → ECUReset). This is the documented black-screen cause.
- **Registers read clean** → the latch is *not* the live problem and this tool
  cannot fix the screen.
- **Not answering** → the unit is off the bus; ISTA only.

### When this tool cannot fix it

EnergyMode is a volatile register, so a unit that has sat through full sleep
cycles has very likely cleared it already. If the screen is still dark after
that, the latch has been outlived and the recovery is external.

Every upstream report that recovered used an external tool, not a state clear:

- reflash the HU — **BTLD + SWFL + CAFD, skipping ibaDeploy** (the most specific
  report, and it fixed the black screen)
- E-Sys generally
- Feature Installer

On a late branch like `Y`, whoever reflashes needs **PSdZData current enough for
that line** — flashing it with older data is its own version mismatch.

### Do not use `--revert` to fix a dark screen

`--revert` goes through the *same* flash-mode entry that failed. Upstream has a
report of both UNLOCK and REVERT failing with the same log. It will not fix the
screen and risks another bad exit.
