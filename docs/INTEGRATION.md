# TBW NBT Evo Toolbox — integration map (shop/dev)

This repo merges three open-source projects into one end-user procedure. The wizard
(`source/index.html`) is the single UI served by the Go app. The gauge engine stays in
Go; the SSH / self-check / CarPlay stages are a **tested Python backend**
(`tools/toolbox.py`) that the Go server shells out to via a thin dispatcher
(`source/toolbox.go`). One app, one build — all new logic lives in Python so it's
verifiable without the Go toolchain.

## Stage → tool → endpoint

| Wizard stage | Backend endpoint | Implemented by | Status |
|---|---|---|---|
| (launch) self-check | `selfcheck` | `tools/toolbox.py` | ✅ implemented + tested |
| 1 Connect | `discover` | `enet.go` (existing) | ✅ works today |
| 2 Enable SSH | `unlock` `{selftest|commit|revert}` | `tools/toolbox.py` → `tools/ssh-unlock/` | ✅ implemented; flash path needs a car |
| 3 Identify | `inspect` (existing) | `install.go` inspect | ✅ works today; `hu_info.py` additive |
| 4 Install gauges | `install` / `recover` | `install.go` (existing) | ✅ works today |
| 5 Full-screen CarPlay | `carplay` | `tools/carplay_fsc.py` | ⛔ gated — needs verified profile |
| 5b Activate CarPlay | `carplay-activate` | `tools/carplay_fsc.py` | ⛔ gated — needs verified profile / FSC |
| 6 Secure | `unlock` `{revert}` | `tools/toolbox.py` → `tools/ssh-unlock/` | ✅ implemented; needs a car |

The UI degrades gracefully: any endpoint that returns 404 shows a "needs app build"
message, so a gauge-only build still works.

## How the backend is wired (implemented)

`source/main.go` routes `selfcheck|unlock|carplay|carplay-activate` to `runToolbox()`
(`source/toolbox.go`), which runs `python -I tools/toolbox.py <op>` with the JSON request
body on stdin and returns the JSON result. The backend is stdlib-only; OpenSSH and Python 3
ship on Win10/11 and macOS.

`tools/toolbox.py` ops (reads JSON on stdin, writes JSON on stdout):
- **selfcheck** → `{Ok, Checks{python, ssh, enet_link, tools_present, flasher_selftest}, Warnings}`.
  Runs on launch; proves the environment and the unlock engine before any car is touched.
- **unlock** → `{selftest}` runs the offline self-test; `{commit, Gateway}` / `{revert, Gateway}`
  drive `ssh-unlock/nbt_optdebug_flash.py --commit|--revert --host <gw> --no-discover`
  (200 s cap, under the server's 4-min write timeout). Returns `{Ok, Vin?, Result}`.
- **carplay** / **carplay-activate** → `tools/carplay_fsc.py`. Both back up on-device state,
  then apply **only** a profile verified for the firmware; with no profile they return
  `{Pending:true}` and write nothing. See CARPLAY_ACTIVATE_FSC.md.

Packaging: ship the built app **with the `tools/` directory beside it** (the dispatcher looks
next to the executable, then the source tree). `hu_info.py` remains available as a CLI probe.

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
