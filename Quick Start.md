# TunedByWali · iDrive 6 Gauge Installer — Quick Start

A simple PC app that installs **BoostDeck**, **ShiftDeck**, and **MHD Maps** onto your BMW
**NBT Evo (iDrive 6)** head unit. Everything runs locally on your computer, over the cable
plugged into your car. Nothing is sent anywhere.

> Supported firmware (this preview): **NBTevo_N18092I**, **NBTevo_V20153I**, **NBTevo_Y24073J**.
> Any other version is blocked automatically — the installer will not write to it.

---

## What you need

- Your car **parked**, ignition on, iDrive fully awake (keep it awake the whole time).
- A **wired ENET cable** from the car's OBD port to your computer. A USB‑to‑Ethernet
  adapter is fine if your laptop has no Ethernet port.
- The **Bavarian SSH Client** to turn on temporary SSH and give you a root password:
  <https://ssh.bavarian-aa.uk/>
- 10–15 quiet minutes with the car.

---

## Step by step

1. **Download and open the installer**
   - Windows: unzip `NBT-Evo-Gauges-Windows-x64.zip` and run the `.exe`.
   - Mac: unzip the macOS build and open the app.
   - A window opens in your browser automatically. Leave the installer app running.
   - *(Windows may warn that the app is unsigned, and Mac that it's from an unidentified
     developer. That's expected for a small shop tool — allow it to run.)*

2. **Connect the cable**
   - Plug the ENET cable into the OBD port and your computer. Make sure iDrive is on.

3. **Enable temporary SSH**
   - Open the **Bavarian SSH Client**, let it find your car, confirm it picked the right
     head unit, and choose **Enable SSH**. Note the **root password** it gives you.
   - *(Temporary SSH turns off again when iDrive restarts — that's normal.)*

4. **Find your car** (Step 1 in the installer)
   - Press **Find my car**. The head‑unit and gateway IPs fill in automatically.
   - If only the gateway fills in, type the **head‑unit SSH IP** shown by the Bavarian tool.

5. **Unlock & verify** (Step 2)
   - Enter the **root SSH password** from Step 3.
   - Leave resolution on **Automatic** unless the installer asks you to pick one.
   - Press **Check compatibility**. The first time, it shows an **SSH fingerprint** —
     confirm the IP is your car, then press **Trust this fingerprint & check**.
   - Wait for **"ready for a parked preview."** If it says the firmware isn't supported or a
     fingerprint doesn't match, **stop** — don't force it. Send a screenshot to your
     TunedByWali contact.

6. **Install** (Step 3)
   - Press **Install all three apps**. Keep iDrive on and the cable connected.
   - Wait for **"Installed and verified all three apps."**

7. **Restart iDrive**
   - While parked, **press and hold the volume/power knob** until the screen goes dark
     (about 30 seconds), then release. Wait for iDrive to load.
   - Open **My Vehicle** — you'll see **BoostDeck**, **ShiftDeck**, and **MHD Maps**.

---

## Good to know

- **It keeps a backup.** If an app was already installed, the old copy is saved to a backup
  folder shown on screen. Note that path before you restart.
- **Check before you trust the numbers.** This is a parked preview. Compare gauge values
  against a known‑good logger before relying on them. BoostDeck's signal definitions come
  from a 2016 340i, so some cars show fewer signals.
- **MHD Maps** needs a compatible **MHD+ multi‑map tune** already on the car. Always confirm
  the active slot in MHD after switching.
- **Tip:** in a gauge, press the **OPTIONS** button on the iDrive controller for color and
  display settings.

## If something goes wrong

- **"Install stopped"** — leave iDrive running and the cable connected. The installer keeps
  a backup and can roll back. Re‑read the on‑screen message to your TunedByWali contact.
- **An app misbehaves after restart** — re‑enable SSH, then your installer/contact can move
  the app out of `/var/opt/hmi/data/ID5/Plugins` and restore the backup.
- Only install on a vehicle **you own or are authorized to modify.**

**Support:** message **b58devkev** on Instagram, or your TunedByWali / TBW Performance contact.
