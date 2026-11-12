# Upstream: BMW HU Info

`hu_info.py` is a clean-room, read-only **Identify** pass inspired by the capability set
described in **Viaszx/BMW_HU_Info** (https://github.com/Viaszx/BMW_HU_Info):

- vehicle / head-unit identification, key info, extensive error reads
- open/close SSH with a private key
- NCD coding read/write (Evo/Enav), display test + audio control
- FSC read / batch-write and an FSC generator for MGU / NBTEVO / ENAVEVO
- NBT Evo CID 12" patch support

That project ships no code yet (README only). This module implements **only** the
read-only identify pass, from head-unit paths already known-good for NBT Evo ID5/ID6
(version file, `uname`, LayerManagerControl, framework libraries, plugin dir). Coding,
FSC and any write capability are intentionally **not** reimplemented here — route those to
the upstream project or a licensed tool when needed.
