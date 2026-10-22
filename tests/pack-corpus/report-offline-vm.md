# Offline VM deployment replay

Date: 2026-10-22.

## Environment

- Existing libvirt VM `ubuntu22.04` (Ubuntu 22.04 userland, kernel 6.8),
  6 vCPU / 8 GB RAM. This is **not** a freshly provisioned clean VM.
- The VM's Qt is 6.2, below the project's Qt 6.5 floor, so the app was run as
  a portable bundle built on the Arch host (Qt 6.12): binary, Qt libraries,
  plugins and QML modules under `/mnt/projects/bmcd-portable`, loaded through
  the bundled `ld-linux-x86-64.so.2`. The bundle is a distribution artifact,
  not a from-source VM build.
- Both VM NICs were set link-down with `virsh domif-setlink` for the run.

## Sample and staging

- Modrinth sample: `Fabulously Optimized` 14.1.1, Minecraft 26.2 / Fabric
  0.19.5, 51 manifest files, 108 files after overrides, 43.2 MB.
- Import and file prefetch were done on the host before disconnecting; the
  shared staging tree was already populated. This validates deploy/verify
  offline, not offline import.

## Result

- Network probe: `curl https://api.modrinth.com` failed with DNS resolution
  (`Could not resolve host`), so the VM was genuinely offline.
- Deploy: `bmcd --pack stage/import.json --to game --no-backup` exited 0 and
  served 108 files (43.2 MB).
- Verify: `bmcd --pack stage/import.json --verify game` exited 0 with
  `present 108, missing 0, corrupt 0`.
- Raw log: `vm-offline-report.txt`.

## Limits

This replay closes the "deploy/verify does not need the network" question in a
real VM, but it is **not** the 1.4.4 clean-VM sign-off:

- The VM was reused, not freshly installed.
- Import and prefetch happened before the network was cut.
- The app was a host-built portable bundle, not built inside the VM.
- No Minecraft launch / external launcher was exercised.

The remaining sign-off still needs a clean VM with the target Minecraft
version installed, offline import+deploy from a cold cache, and an external
launcher starting the deployed profile.
