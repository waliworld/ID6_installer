# TunedByWali · iDrive 6 Installer

A lightweight PC program that lets a client install **BoostDeck**, **ShiftDeck**, and
**MHD Maps** onto their BMW **NBT Evo (iDrive 6 / ID6)** head unit — with a simple,
guided, TunedByWali‑branded wizard instead of a developer tool or an AI agent.

> **Client guide:** [Quick Start.md](Quick%20Start.md)

---

## What this fork adds

This repository is a fork of the open‑source **NBT Evo Gauges** project. The upstream project
ships a working flashing engine but a developer‑oriented interface — its README tells clients
to *"hand this repository to an AI agent."* That's not something a customer can do on their own.

This fork keeps the upstream engine **unchanged** and replaces only the front end:

- **`source/index.html`** — a rebuilt, branded, **3‑step wizard** (Connect → Verify → Install).
  - Responsive for phone and desktop, large touch targets, minimal typing.
  - Auto‑fills the car's IPs, clear step gating, plain‑language status and error states.
  - Drives the **same** `discover` / `inspect` / `install` backend endpoints — so the tested
    install logic, framework‑hash safety gate, install‑lock, and automatic rollback are
    untouched.

Nothing about *how the car is flashed* was changed. Only the experience of using it.

## How it works (the engine, unchanged from upstream)

1. **ENET discovery** — finds the car's diagnostic gateway and head unit over the wired
   OBD/ENET link (169.254.x.x).
2. **Inspect** — SSHes into the head unit and verifies:
   - firmware is one of the three supported `NBTevo_*` builds,
   - the unit reports **QNX 6.5.0 ARM**,
   - both on‑car framework files match known **SHA‑256** fingerprints,
   - the display resolution is a supported layout,
   - the ID5 `Plugins` folder is writable and the gateway is reachable.
   - Installation stays **disabled** unless every check passes.
3. **Install** — stages all three apps, scales the layout XML to the display, publishes behind
   an **install lock**, reads every file back to verify it, and keeps a **backup** of anything
   it replaces. On any failure it **rolls back** automatically.

## Supported firmware

`NBTevo_N18092I`, `NBTevo_V20153I`, `NBTevo_Y24073J`. Anything else is refused by design.
Bringing up a new firmware requires the upstream compatibility workflow and a parked test
on an authorized car — see [`AgentReadMe.md`](AgentReadMe.md).

---

## Building the client installer (shop side)

The finished app is a tiny local web server (Go) that embeds this UI and auto‑opens the
browser. Clients just run the `.exe` / `.app` — no toolchain on their end.

**Requirements on the build machine:**
- **Go 1.27.3** (matches `source/go.mod`) with its `golang.org/x/crypto` and `golang.org/x/sys`
  modules available.
- The **private QNX app payload** under `source/payload/` (the compiled `BoostDeck`,
  `ShiftDeck`, `MhdMaps` folders and the `tools/` helper). These are **intentionally not in
  this public repo** — see `source/docs/BUILD.md` and `source/payload/README.md`. Without the
  payload the installer builds but has nothing to install.
- macOS with `lipo` / `codesign` if you want the universal, ad‑hoc‑signed Mac build.

**Build:**

```sh
cd source
# stage the private payload per source/docs/BUILD.md, then:
./build-release.sh            # outputs signed zips into source/dist/
```

This produces `NBT-Evo-Gauges-Windows-x64.zip`, `NBT-Evo-Gauges-macOS-universal.zip`, and
`SHA256SUMS.txt`. Rename/brand the zips for distribution as you like, publish them on a
GitHub Release here, and hand clients the download plus [Quick Start.md](Quick%20Start.md).

To preview the UI without a car, run the server locally and open the printed URL:

```sh
cd source && go run . -no-browser
```

---

## Safety & authorization

- Install only on vehicles **you own or are authorized to modify.** The runbook assumes owner
  consent for every live operation.
- This is a **parked preview.** Verify gauge accuracy and map switching on each car before
  relying on them. MHD Maps requires a compatible **MHD+ multi‑map tune**.
- Temporary SSH is enabled with a separate third‑party tool
  ([Bavarian SSH Client](https://ssh.bavarian-aa.uk/)); it closes again on an iDrive restart.

## Credits

Flashing engine, app payloads, and compatibility work: the upstream **NBT Evo Gauges**
project (see `source/third-party/` and `source/LICENSE`). Branded client wizard and
packaging for clients: **TunedByWali · TBW Performance (Built by Wali LLC).**

Issues or wins: message **b58devkev** on Instagram.
