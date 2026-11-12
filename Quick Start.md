# TunedByWali · NBT Evo Toolbox — Quick Start

One app, one procedure for BMW **NBT Evo (iDrive 5/6)** head units:

**1 Connect → 2 Enable SSH → 3 Identify → 4 Install gauges → 5 Full-screen CarPlay → 6 Secure**

> Runs on **NBT Evo `007.xxx`** units. **Do not** run the SSH step on a `001_05x` unit.
> Gauges also need a supported HMI build (`NBTevo_N18092I / V20153I / Y24073J`) — the
> Identify step checks this for you.

---

## What you need

- Car **parked**, ignition on, iDrive awake the whole time.
- A wired **ENET (OBD-to-Ethernet) cable**; your adapter should show a `169.254.x.x` address.
- 15–20 quiet minutes.

## The steps

1. **Open the toolbox and connect.** Run the app (a window opens in your browser; leave the
   app running). Plug in the ENET cable and press **Find my car** — the IPs fill in.

2. **Enable SSH.** This flashes a debug config so the head unit allows root SSH after a
   reboot. ⚠️ **It writes to the unit and can disable it if it's the wrong type — only run on
   an NBT Evo 007.xxx unit.** Press **Run offline self-test** (expect a pass), tick the
   confirmation, type `UNLOCK`, then **Enable SSH**. Wait ~50 seconds for the reboot. Keep the
   cable connected.

3. **Identify the unit.** Press **Identify**. You'll see the firmware, display, whether the
   gauges are supported, and which **CarPlay** method your unit needs. If it shows a
   fingerprint first, confirm the car and choose **Trust & identify**.

4. **Install the gauges.** Press **Install all three apps** (enabled only if your build is
   supported). It keeps a backup of anything it replaces — **note the backup path** shown in
   Status.

5. **Full-screen CarPlay.** The toolbox shows what your firmware needs:
   - **Coding firmware (newer):** set it in your coding tool (E-Sys / BimmerCode) — no patch needed.
   - **Earlier firmware:** press **Enable full-screen CarPlay** (uses the SSH you enabled).
   - *If the unit has no CarPlay at all,* its CarPlay FSC must be activated first (separate step).

6. **Secure & finish.** Restart iDrive (hold the volume/power knob ~30s while parked), then
   check the gauges, map switching and CarPlay. Finally press **Close SSH (restore factory)**
   so the unit isn't left open as root.

---

## If something goes wrong

- **SSH flash won't complete:** nothing partial is left — the stock config is intact. Re-read
  the Status message to your TunedByWali contact.
- **Unit won't boot after enabling SSH:** use **Close SSH (restore factory)** first; serial/eMMC
  recovery is the last resort.
- **A gauge or CarPlay misbehaves:** re-enable SSH and restore the backup shown during install.
- Install only on a vehicle **you own or are authorized to modify.**

**Support:** **b58devkev** on Instagram, or your TunedByWali / TBW Performance contact.
