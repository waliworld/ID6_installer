# Working on this project

[AgentReadMe](../AgentReadMe.md) covers setup, automation, and unlisted firmware.

- Read [PORTING.md](docs/PORTING.md), [SIGNALS.md](docs/SIGNALS.md), and [BUILD.md](docs/BUILD.md) before changing diagnostic definitions or head-unit compatibility.
- Start with host tests. Keep live checks parked and bounded. Use the read-only probe for signal discovery. Ask the owner before a live installation, reboot, or ECU write if the current request has not authorized it. Map switching is a separate opt-in parked test.
- Keep firmware/ABI, display layout, and ECU signal changes separate. Use a known working control read, record raw request and response, and validate the candidate against an independent reading. Preserve the existing profile and leave unverified values blank.
- Keep application names, model and action IDs, and connectors consistent across generators, manifests, and code. Regenerate the resource archives after layout changes.
- Keep VINs, credentials, personal captures, proprietary framework binaries, and generated native outputs outside the public source commit. Use only the required local files for ABI work.
