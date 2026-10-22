# 1.4.4 corpus status

Date: 2026-10-22 (Linux workspace).

| format | import | deploy | export | verify | note |
|---|---:|---:|---:|---:|---|
| Modrinth (20 real `.mrpack`) | 20/20 | 1/1 sample | 1/1 sample | 1/1 sample | sample: Fabulously Optimized, 51 files, 0 missing/corrupt |
| FTB (20 live metadata zips) | 20/20 | 0/20 | 0/20 | 0/20 | `dist.modpacks.ch` ~6 KB/s; first deploy still running after 10 min |
| CurseForge | 0/20 | — | — | — | blocked on `CURSEFORGE_API_KEY` |

Offline rehearsal: `offline-check.sh` deployed 108 files and verified 108/108
inside `bwrap --unshare-net`. This proves the deploy/verify path is network-free
after staging, but it is still not the clean-VM sign-off required by I4.

Offline VM replay: an existing Ubuntu 22.04 libvirt VM had both NICs set
link-down; deploy served 108 files and verify reported `present 108, missing 0,
corrupt 0`. See `report-offline-vm.md`. The VM was reused, import/prefetch ran
before the disconnect, and the app was a host-built portable bundle, so this is
not yet the clean-VM sign-off either.

The FTB corpus uses a generated `manifest.json` container because the public
API exposes only per-file URLs, not an official archive. The Modrinth archives
are real release files and their URL/SHA-1 are recorded in the generated
manifest.

## Code finding

FTB's API returns unescaped spaces in file URLs. Import used to store the raw
string; `PackImporter::firstDownload()` now calls
`QUrl::toString(QUrl::FullyEncoded)`. Regression: `TestPack::ftbEncodesUrlSpaces`.

## Verdict

1.4.4 is **not signed off**. Remaining work:

1. Provide `CURSEFORGE_API_KEY` and fetch/deploy 20 CF packs.
2. Run all 20 FTB deployments from a network or mirror with usable throughput.
3. Run the self-contained A offline acceptance on a clean VM with the same
   Minecraft version already installed and networking disabled.
