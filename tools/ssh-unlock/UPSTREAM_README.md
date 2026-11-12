# NBT-Evo SSH Unlocker

A small, user-friendly GUI that enables debug access (SSH / telnet / qconn) on a
BMW **NBT-Evo** head unit. It flashes a patched `starter.cfg` (OPT_DEBUG
`STOP` -> `RUN`) directly to the unit's live partition over the diagnostic
network, and can also restore the stock config.

> **Use at your own risk.** This writes to a unit inside a real car. A wrong or
> version-mismatched file can leave the head unit unable to boot. Recover with
> **Restore factory (revert)** first, then serial/eMMC as the last resort.

This tool **only enables SSH/debug access**. It does not touch certificates or
FSC codes (see [Background](#background)).

---

## What it does

- Reaches the HU flash bootloader over BMW **HSFZ** (TCP 6801; discovery via
  UDP broadcast on 6811).
- Performs UDS `SecurityAccess 0x27` Level 3 using the inlined L3 key.
- Writes the OPT_DEBUG `starter.cfg` with **LIVE-DIRECT** file copy (no
  signature record, so the SWDL signature check is bypassed).
- After the reboot, `sshd` (22), `telnetd` (23) and `qconn` (8000) come up as
  **root** over OBD/ENET.

Supported: **NBT-Evo 007.xxx** family (`007_001_016 / 030 / 039 / 049 / 051`).
**Do NOT use on a `001_05x` unit** (different `starter.cfg`).

---

## Requirements

- Windows 10/11 (the GUI uses Tkinter, which ships with Python on Windows).
- Python 3.12 (any recent Python 3 works).
- ENET/OBD cable and a network adapter with a `169.254.x.x` link-local address.
- The HU powered and reachable (ignition/terminal state as required by your setup).
- Allow Python through the Windows firewall when prompted.

Install Python with winget if needed:

```powershell
winget install --id Python.Python.3.12 -e --source winget
```

Then open a **new** terminal so `python` / `py` resolve to the new install.

---

## Quick start

1. Put both files in the same folder:
   - `nbt_ssh_unlock_gui.py` (the GUI)
   - `nbt_optdebug_flash.py` (the flasher it wraps)
2. Start the GUI:
   - double-click `Start_SSH_Unlock.bat`, or
   - run `python nbt_ssh_unlock_gui.py`
3. Click **Offline self-test** first. You should see `ALL PASS`.
4. Connect the ENET cable and make sure your adapter has a `169.254.x.x` address.
5. Click **Detect** (or type the gateway IP) and then **Test**. A valid unit
   returns its VIN.
6. Tick **I understand the risk**.
7. Click **Unlock SSH** and type `UNLOCK` to confirm.
8. Wait ~50 seconds for the HU to reboot, then connect:

   ```
   ssh root@169.254.199.119
   ```

   Ports `22`, `23` (telnet) and `8000` (qconn) are open; user `root`, no password.

---

## Restoring the factory config

If the unit misbehaves after unlocking, or you simply want to close SSH again:

1. Connect to the unit the same way.
2. Tick the safety checkbox.
3. Click **Restore factory (revert)** and type `REVERT`.
4. Wait for the reboot and verify that SSH/telnet/qconn are closed.

The revert uses the same LIVE-DIRECT path and writes the byte-exact stock
`starter.cfg`.

---

## Advanced fields

Leave these at their defaults unless you know what you are doing.

| Field | Default | Meaning |
| --- | --- | --- |
| Gateway address | *(empty)* | Empty = auto-discover via the 6811 broadcast; falls back to `169.254.199.99`. |
| Port | `6801` | HSFZ diagnostic port. |
| LBN (hex) | `0` | Logical block number for erase / checkMemory. |
| Tester ID (hex) | `00000000` | UDS tester address used in the L3 seed signature. |
| Controller (hex) | `00001ffd` | Bootloader controller ID (NBT-Evo BTLD `00001FFD`). |
| Fingerprint (hex) | `00` x 13 | Fingerprint written with `2E F15A`. |
| Reset timeout (s) | `50` | How long to wait for the HU to reappear in programming session. |

---

## How it works (short version)

1. Enter the flash bootloader: `10 03` -> `31 01 0F0C 03` (EnergyMode=Flash) ->
   `10 02` (programming session). The HU reboots into the bootloader.
2. Authenticate with `27 11` / `27 12`. The key is the L3 BMWSigVerify signature
   over `MD5([UserID][Controller][seed])` (RSA-1024, e=3).
3. SWDL write: `31 FF00` erase, `34` requestDownload, `36` transferData,
   `37` transferExit, `31 0202` checkMemory.
4. The container targets `/fs/sda0/opt/sys/etc/starter.cfg` **LIVE-DIRECT** and
   carries no signature record, so it is applied to the running filesystem.
5. Finalize: ActivateApplicationMode, clear Energy/ExtendedMode, reset.

The offline **self-test** verifies the L3 key modulus hash, signs test seeds,
and checks the SHA-256 of both inlined containers before any car is touched.

---

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `No gateway detected` | Check the ENET cable, adapter link-local address (`169.254.x.x`), and firewall. |
| Detect works but **Test** fails | Confirm the gateway IP/port and that nothing blocks TCP 6801. |
| Flash aborted | Nothing was written; the existing `starter.cfg` is intact. Check the log. |
| Unit does not boot after unlock | Run **Restore factory (revert)**; if that is impossible, recover over serial/eMMC. |
| Self-test fails | One of the bundled blobs/key is corrupted; re-download the tool. |

---

## Background

Unlike FDL coding, FSC coding requires a certificate signed by BMW that the NBT
checks against a public key stored on the device. One way to make the unit
accept a self-signed FSC is to replace the on-device public key with your own
CA, then sign FSCs yourself. Enabling SSH is the first step: it gives write
access to the filesystem so the CA modulus can be replaced, after which e-sys
can apply your own FSC codes. See the original reversing notes in the repository
history for the certificate-format details.

---

## Disclaimer

This project is for research and personal use on hardware you own. You are
responsible for complying with local laws and for any damage to your vehicle or
head unit. There is no warranty.
