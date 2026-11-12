# Coding & FSC (nbt-evo-toolkit)

The toolbox's coding and FSC work is backed by the **MIT-licensed nbt-evo-toolkit**, vendored
at `third_party/nbt-evo-toolkit/` (commit in `.vendored-commit`). This replaces the earlier
"gated placeholder" CarPlay path with a real, honest engine. Bridge: `tools/coding/toolkit_bridge.py`.

## What's real, and the honest boundary

The toolkit ships **no BMW data or keys**. You supply your own **PSdZData** (your licensed BMW
config distribution), and it recovers *your* signing key from it, locally. It enforces an
**owner-VIN consent gate**: writes and key recovery refuse to run unless `NBTEVO_OWN_VIN` is set
to the VIN of the car you own/are authorized to modify, and (for writes) matches the connected car.
**We never bypass that gate.** Reads are free; writes are gated.

| Op (toolbox) | What | Runs offline? |
|---|---|---|
| `fsc-selftest` | prove the FSC mint/verify/recovery model | ✅ yes (verified) |
| `fsc-catalog` | application-number → feature (CarPlay `0x0143`, …) | ✅ yes |
| `fsc-keygen` | your own e=3 trust anchor + 128-byte blobB | ✅ yes |
| `fsc-mint` | mint an FSC under your key | ✅ yes |
| `coding-identify` | read BTLD/CAFD/VIN + scheme | needs car |
| `coding-backup` | dump live coding to an NCD backup | needs car |
| `carplay-activate` | the ordered CarPlay recipe + local mint | mint offline; writes = runbook |

## The FSC trust-anchor model (why this is honest, not forgery)

Every "long" FSC is checked against **one** RSA modulus (**blobB**) compiled into `NBTCarHU`.
You do **not** hold BMW's FSC private key, so you **cannot** forge a BMW-signed FSC. What you
**can** do, on a unit you own: replace blobB with **your own** modulus (a 128-byte length-
preserving patch at `NBTCarHU 0x35A300`) and then mint FSCs under **your own** key — the unit
accepts them because the installed trust anchor is now yours. Coding is keyed separately and is
unaffected.

> Enabling a feature BMW hasn't licensed to you may breach your BMW agreement even on your own
> car — that's between you and BMW. Owner's car only, bound to your VIN. See the toolkit's LEGAL.md.

## CarPlay activation (unit has no CarPlay) — the ordered recipe

Per the toolkit's `examples/carplay.md`, CarPlay needs **both** an entitlement and a coding bit:

1. **Back up** coding (`coding-backup`) and current FSCs.
2. **Mint** the CarPlay FSC `0x0143` under your trust anchor (`fsc-keygen` once, then `fsc-mint`) — runs locally now.
3. **Install your blobB** into `NBTCarHU` (flash_and_patching runbook).
4. **Apply** the minted FSC over the `Swt` service (storeFSC runbook).
5. **Set `CARPLAY_SWT`** via a signed coding write (`capabilities/coding` — needs PSdZData + consent VIN).
6. **Reboot**, confirm CarPlay; revert from the backup if the HMI doesn't come up.

Steps 2 and 5 are coded (mint now; coding write is the toolkit's fully-worked capability). Steps 3–4
are the toolkit's documented car-write runbooks. Full-screen CarPlay on newer firmware is just the
coding half (`CARPLAY_SWT`), no FSC needed.

## Setup

```sh
cd third_party/nbt-evo-toolkit && pip install -e .     # make the shared package importable
export NBTEVO_OWN_VIN=YOURVIN                           # the car you own/are authorized on
export NBTEVO_PSDZDATA=/path/to/your/psdzdata           # your licensed copy (not shipped)
```
The toolbox finds the toolkit at `third_party/nbt-evo-toolkit` or `$NBTEVO_TOOLKIT`. Owner FSC keys
land in `fsc-keys/` and are git-ignored — that key is your root of trust; keep it private.
