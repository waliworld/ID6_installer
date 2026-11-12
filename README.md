# TunedByWali · NBT Evo Toolbox

A lightweight PC program that walks a client through the **whole** NBT Evo (iDrive 5/6)
job as one guided procedure:

**Connect → Enable SSH → Identify → Install gauges → Full-screen CarPlay → Secure**

> **Client guide:** [Quick Start.md](Quick%20Start.md) · **Build/dev:** [docs/INTEGRATION.md](docs/INTEGRATION.md)

---

## What it does

- **Enable SSH** — flashes a patched `starter.cfg` over the diagnostic network so root SSH
  comes up on the head unit; reverts to stock when you're done. *(tools/ssh-unlock)*
- **Identify** — read-only read-out of firmware, display, framework match, installed apps,
  and which CarPlay method the unit needs. *(tools/hu-info + the Go `inspect` check)*
- **Install gauges** — BoostDeck, ShiftDeck and MHD Maps, with backup + automatic rollback.
  *(the existing Go engine — unchanged)*
- **Full-screen CarPlay** — routes by firmware: a coding parameter on newer builds, or an
  SSH aspect-ratio patch on older ones (the case the enabled SSH unlocks).
  *(see [docs/CARPLAY_FULLSCREEN.md](docs/CARPLAY_FULLSCREEN.md))*
- **Secure** — closes SSH back up so a customer's unit isn't left open as root.

One branded wizard (`source/index.html`) is the whole UI; the stepper gates each stage and
every risky action is confirmed. It's responsive for phone and desktop.

## Self-reliant backend

The app validates itself on launch (**self-check**: Python, OpenSSH, ENET link, bundled
tools, and an offline self-test of the SSH flasher) and runs every stage itself. The Go
server owns the gauge engine; the SSH / self-check / CarPlay stages are a tested Python
backend (`tools/toolbox.py`) the server shells out to via `source/toolbox.go` — one app,
one build. See [docs/INTEGRATION.md](docs/INTEGRATION.md).

| Stage | Status |
|---|---|
| Self-check (launch) | ✅ implemented + tested |
| Connect, Install gauges, Identify (core) | ✅ work today (Go engine) |
| Enable / Close SSH | ✅ implemented (drives the vendored flasher); flash needs a car |
| Full-screen CarPlay / Activate CarPlay | ⛔ implemented but **gated** — applies only a profile verified on a reference car; otherwise reports "pending" and writes nothing |

The wizard degrades gracefully: any stage whose backend isn't present shows a clear
"needs app build" note instead of breaking, so a gauge-only build still ships.

## Two firmware gates (don't assume overlap)

- **SSH unlock** supports the **`007.xxx`** starter.cfg family (not `001_05x`).
- **Gauge install** supports specific HMI builds (`NBTevo_N18092I / V20153I / Y24073J`).

A car can pass one and not the other; Identify surfaces both before anything is flashed.

## Building the installer (shop side)

Clients just run the `.exe` / `.app`. To build it: **Go 1.27.3**, the private QNX
`payload/` (the compiled gauge apps — not in this public repo; see `source/docs/BUILD.md`),
and Python 3 + OpenSSH present at runtime for the SSH/identify stages. Then:

```sh
cd source
./build-release.sh        # Windows x64 + universal macOS zips, with SHA-256 sums
```

Preview the UI without a car: `cd source && go run . -no-browser`.

## Safety & authorization

- **Enable/Close SSH is a live-partition flash — brick-capable on the wrong unit.** Keep the
  offline self-test mandatory; `--revert` is first recovery, serial/eMMC the backstop.
- This is a **parked preview** — verify gauges, map switching and CarPlay on the car.
- Install only on vehicles **you own or are authorized to modify**, and record who approved it.

## Credits

Gauge engine: upstream **NBT Evo Gauges**. SSH unlock: **Xayanix/nbt-evo-ssh-unlocker**
(vendored). Identify/HU capabilities: **Viaszx/BMW_HU_Info**. Branded toolbox and
integration: **TunedByWali · TBW Performance (Built by Wali LLC).**

Issues or wins: message **b58devkev** on Instagram.
