# Full-screen CarPlay on NBT Evo

## The two cases (decided by firmware)

Full-screen CarPlay behaves differently depending on the NBT Evo HMI build, and the
Identify step routes each unit automatically:

- **Later builds (letter X / Y and up):** full-screen is a **coding parameter** (FDL /
  E-Sys / BimmerCode). No filesystem change is needed, so the toolbox just tells the
  operator to set it in their coding tool. This is community-documented behavior.
- **Earlier builds (before X — e.g. N, V):** full-screen is **not** exposed as coding.
  The HMI **aspect ratio has to be changed over SSH** (on some builds it can be done from
  a USB stick). This is exactly why the toolbox enables SSH first — the same root access
  that installs the gauges is what lets us reshape CarPlay on older units.

The routing table lives in `tools/hu-info/hu_info.py` (`carplay_method`) and is mirrored
in the wizard. It is a **heuristic by build letter** — confirm against the actual unit.

## Prerequisite: CarPlay must already be active

Full-screen only *reshapes* CarPlay that is already running. If a unit never had CarPlay,
its CarPlay **FSC** (BMW Freischaltcode) must be activated first — a separate, licensed
step (the BMW HU Info project targets FSC read/write/generation for this). Once CarPlay
runs at all — even half-screen — the full-screen step applies.

## Before enabling the `carplay` endpoint — verify on a reference car

The exact aspect-ratio change for the SSH path is **intentionally not hard-coded** in this
repo. Writing an unverified HMI config value can break CarPlay rendering. Before wiring the
`carplay` endpoint for production:

1. On a parked, authorized reference car of a known earlier build, enable SSH (Step 2).
2. Capture the stock CarPlay/HMI aspect-ratio config (keep a byte-exact backup).
3. Apply the aspect-ratio change, confirm full-screen renders correctly, and confirm you
   can restore the backup cleanly.
4. Record the exact file/path/value and the builds it's valid for, then implement `carplay`
   with the same backup + read-back + rollback discipline as the gauge install.

Until that is done, the wizard shows the step as "pending" rather than flashing a guess.
