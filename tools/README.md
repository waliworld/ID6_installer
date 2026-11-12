# tools/ — bundled capabilities

These are the extra capabilities folded into the TBW NBT Evo Toolbox. See
`docs/INTEGRATION.md` for how each one wires into the wizard.

## ssh-unlock/ — enable/close root SSH
Vendored from **Xayanix/nbt-evo-ssh-unlocker**. Flashes a patched `starter.cfg`
(OPT_DEBUG STOP→RUN) over the diagnostic network so `sshd`/`telnet`/`qconn` come up as
root; `--revert` restores the byte-exact stock config. Standalone Python, stdlib only.

- `nbt_optdebug_flash.py` — the flasher. `--selftest` (offline), `--commit` (flash),
  `--revert` (undo). DRY-RUN by default.
- `nbt_ssh_unlock_gui.py` — upstream's own Tkinter GUI (kept for reference/standalone use).
- `UPSTREAM_README.md` — upstream docs, supported firmware (`007.xxx`), and recovery notes.

⚠️ Live-partition flash — brick-capable on the wrong unit. Only `007.xxx`, never `001_05x`.

## hu-info/ — read-only Identify
`hu_info.py` reads the unit over SSH (firmware, display, framework fingerprints, installed
apps, error tail) and reports which full-screen CarPlay method the firmware needs. Writes
nothing. Capability set modeled on **Viaszx/BMW_HU_Info** (which also targets NCD coding and
FSC read/write/generation — not implemented here).
