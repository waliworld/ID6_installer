# Activating CarPlay (FSC) and full-screen on NBT Evo

Two different jobs, both run over the root SSH the toolbox enables, both handled by
`tools/carplay_fsc.py` and exposed as backend ops:

| Job | Op | When |
|---|---|---|
| Make CarPlay **appear** on a unit that doesn't have it | `carplay-activate` | unit was never CarPlay-enabled |
| Make existing CarPlay **full-screen** | `carplay` | CarPlay already runs, boxed/half-screen |

## How activation works (and where the honest boundary is)

Apple CarPlay on NBT Evo is unlocked by an **FSC** (BMW Freischaltcode) — a signed
certificate the head unit checks against a public key stored on the device. There are two
ways to get a unit to accept one:

1. **Install a valid CarPlay FSC** the shop supplies (bought/licensed). The toolbox stages
   the file, backs up the on-device FSC store, applies it, and verifies.
2. **Self-signed path:** with root SSH you can replace the on-device CA public key (modulus)
   with your own, then sign your own CarPlay FSC and apply it. This is the method the SSH
   unlock's own notes describe.

`carplay_fsc.py` performs all of the **safe orchestration** — detect, back up, apply, verify —
but it **does not fabricate FSC data, keys, or on-device paths**. Writing an unverified value
into the FSC/CA store can break the unit. So the actual apply **steps** and any FSC file come
from a **verified profile** you create after testing on a reference car. Until a profile exists
for the unit's firmware, `carplay-activate` and `carplay` return `pending` and write nothing.

> Legal note: generating/self-signing FSCs bypasses BMW's feature licensing. Do it only on a
> vehicle you own or are authorized to modify, and keep a record of authorization.

## Creating a verified profile

1. Copy `tools/carplay_profiles.example.json` to `tools/carplay_profiles.json`.
2. On a parked, authorized reference car of the target firmware, enable SSH (Step 2) and
   capture the stock state (byte-exact backups of the FSC store and any CA/key store, and of
   the HMI aspect-ratio config for full-screen).
3. Work out the exact change (FSC install commands, or the aspect-ratio edit), confirm it
   works, and confirm you can restore the backup cleanly.
4. Fill in the profile block for that firmware:
   - `kind`: `"activate"` or `"fullscreen"`
   - `match`: a substring of the firmware string (e.g. `"NBTevo_N"`)
   - `backup_paths`: on-device paths to back up before touching anything
   - `steps`: the exact remote shell commands, in order (`{fsc}` is substituted with the
     provided FSC file path for activation). Any non-zero step aborts and points at the backup.
5. Re-test end to end on the reference car before using it on customer cars.

The backend already keeps a timestamped on-device backup under
`/var/opt/hmi/data/tbw-backup/…` before running any step, and reports that path so a bad
apply can be rolled back.

## In the wizard

Step 5 shows the right control for the unit: nothing to do (coding firmware), **Enable
full-screen CarPlay** (earlier firmware, once a `fullscreen` profile exists), and **Activate
CarPlay** under "My unit has no CarPlay at all" (once an `activate` profile or FSC exists).
Before a profile exists, these report "pending reference-car verification" rather than acting.
