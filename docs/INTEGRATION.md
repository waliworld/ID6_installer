# TBW NBT Evo Toolbox — integration map (shop/dev)

This repo merges three open-source projects into one end-user procedure. The wizard
(`source/index.html`) is the single UI; each stage calls one backend endpoint. The
gauge-install engine already exists in Go. The SSH, Identify and CarPlay stages are
wired in the UI and need their backend endpoints added to the Go app (each one shells
out to a vendored tool). Nothing here changes the tested gauge-install logic.

## Stage → tool → endpoint

| Wizard stage | Backend endpoint | Implemented by | Status |
|---|---|---|---|
| 1 Connect | `discover` | `enet.go` (existing) | ✅ works today |
| 2 Enable SSH | `unlock` `{selftest|commit|revert}` | `tools/ssh-unlock/nbt_optdebug_flash.py` | ⚠️ endpoint to add |
| 3 Identify | `inspect` (existing) + `tools/hu-info/hu_info.py` | `install.go` inspect + hu_info | ✅ inspect works; identify extras optional |
| 4 Install gauges | `install` / `recover` | `install.go` (existing) | ✅ works today |
| 5 Full-screen CarPlay | `carplay` | SSH aspect-ratio patch (see CARPLAY_FULLSCREEN.md) | ⛔ needs reference-car verification |
| 6 Secure | `unlock` `{revert}` | `tools/ssh-unlock/nbt_optdebug_flash.py` | ⚠️ endpoint to add |

The UI degrades gracefully: any endpoint that returns 404 shows a "needs app build"
message instead of breaking, so shipping the gauge-only build still works.

## Endpoints to add to the Go app

All new endpoints shell out to the vendored Python (OpenSSH + Python 3 ship on
Win10/11 and macOS). Run the flasher with `-I` and pass the gateway as an argument.

- **`POST /…/unlock`**
  - `{selftest:true}` → `python nbt_optdebug_flash.py --selftest` → `{Pass, Result}`.
  - `{commit:true, Gateway, Head}` → `--commit --gateway <ip>` (run detached so an SSH
    drop can't tear the write) → `{Ok, Vin, Result}`.
  - `{revert:true, Gateway}` → `--revert --gateway <ip>` → `{Ok, Result}`.
  - Gate `commit` on the UI's risk-ack + typed `UNLOCK` (already enforced client-side;
    re-check server-side).
- **`POST /…/carplay`** → apply the verified aspect-ratio patch over SSH, with backup +
  read-back, same discipline as the gauge install. **Do not enable until the exact
  patch is confirmed on a reference car** (CARPLAY_FULLSCREEN.md). Until then return 404
  so the UI shows "pending".
- **`identify`** (optional) → run `hu_info.py --head <ip> --json` for the richer read-out.
  The existing `inspect` already returns Version/Display/Ready, which the wizard uses;
  `hu_info` is additive (installed apps, error tail, CarPlay routing echo).

## Firmware intersection — the gating rule

Two different firmware identities are in play and they must both pass:

1. **SSH unlock** supports the **`007.xxx` starter.cfg family** (`007_001_016/030/039/049/051`;
   NOT `001_05x`). This gates Step 2.
2. **Gauge install** supports specific **HMI builds** (`NBTevo_N18092I / V20153I / Y24073J`,
   matched by framework SHA-256 in `install.go`). This gates Step 4.

A car can satisfy one and not the other. Identify (Step 3) surfaces both so the operator
doesn't flash blind. Keep the two allow-lists independent; never assume overlap.

## Safety invariants (keep these)

- `unlock commit/revert` is a **live-partition flash** — brick-capable. Its own `--revert`
  is first recovery; serial/eMMC is the backstop. Keep the self-test mandatory in the UI.
- Always offer **Close SSH (revert)** at the end; don't leave a customer's unit as open root.
- Gauge install keeps backups + auto-rollback — do not bypass.
- Record who authorized the work and keep the backup path shown in Status.

## Credits / provenance

- `source/` gauge engine — upstream **NBT Evo Gauges**.
- `tools/ssh-unlock/` — **Xayanix/nbt-evo-ssh-unlocker** (vendored; see its UPSTREAM_README.md).
- `tools/hu-info/` — identify pass modeled on **Viaszx/BMW_HU_Info**.
