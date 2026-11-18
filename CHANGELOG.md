# Change notes

- **MOST fallback** covers Bootmod3 map switching, Exhaust Flap and Active Sound, plus the dedicated Exhaust Flap submenu in MHD+ Suite. Bootmod3 burble presets and xDrive still require HSFZ.
- **Desktop installers v0.7.1 beta** embed both rebuilt workers.

- **BoostDeck** has faster gauge updates, a new layout, an Active Ethanol gauge, and metric measurements under OPTIONS.
- **ShiftDeck** performance improvements.
- **MHD+ Suite** groups map switching, exhaust flap control, and Max Cooling. The new Max Cooling tiles show the last request accepted by the DME.
- **Bootmod3 Suite** adds a new control menu for compatible CustomROM tunes. Beta.
- **xDrive Control (experimental)** adds an xDelete-style menu for temporary rear-wheel drive on supported DSC variants.
- **Engine Fault Codes** reads engine codes and shows their descriptions when available. It can read codes even when the DME refuses to clear them.
- **HUD Gauges** adds a curved windshield tachometer and numeric boost in PSI, with a persistent on/off checkbox. The packaged build targets the reference 340i with its factory HUD.
- **Dashboard diagnostics** uses the MOST fallback from PR #9 in the packaged workers. A per-app gateway address can be set in gateway-ip.txt.
- **Exhaust Flap** moves under MHD+ Suite → Options, alongside Max Cooling.
- **Desktop installers** add a read-only compatibility check and recognize matching N18092I and P18362I framework captures.
- **xDrive Control (experimental)** distinguishes missing DSC replies, negative replies, and detected DSC indices outside the write allowlist.
