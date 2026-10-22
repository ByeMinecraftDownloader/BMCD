# 1.4.4 pack corpus

This directory drives the real-pack acceptance run for `bmcd --import`,
`--pack <json> --to`, `--format mrpack` and `--pack <json> --verify`.
Archives live in the user cache, never in this repository.

## Corpus sources

| format | source | status |
|---|---|---|
| Modrinth | 20 released `.mrpack` archives selected from the public search API | real archives, URL and SHA-1 recorded |
| FTB | 20 packs from the public `api.modpacks.ch` API | real metadata wrapped in a minimal `manifest.json` zip; the public API has no pack archive endpoint |
| CurseForge | official API (`CURSEFORGE_API_KEY`) | blocked without a key; the product importer also cannot resolve `projectID/fileID` without that key |

`fetch_manifest.py` deliberately does **not** use cfwidget for the product path.
CF packs fetched this way import with unresolved files, so they cannot count as
a deployed corpus until an API key is configured.

## Fetch

```sh
python3 tests/pack-corpus/fetch_manifest.py \
  --formats modrinth,ftb --count 20 \
  --out tests/pack-corpus/manifest.json
```

The cache defaults to `~/.cache/bmcd-pack-corpus` (`BMCD_CORPUS_CACHE`).
FTB entries are generated under `<cache>/archives/` and carry
`"generatedContainer": true` so reports do not present them as official zips.

## Run

```sh
python3 tests/pack-corpus/run.py \
  --manifest tests/pack-corpus/manifest.json \
  --binary build/bmcd \
  --stages import,deploy,export,verify \
  --report tests/pack-corpus/report.json
```

Useful filters: `--format modrinth`, `--limit 5`, `--stages import`.
The runner checks SHA-1 before importing, compares the imported Minecraft/loader
metadata with the manifest expectation, keeps every attempted entry in the
denominator, and writes both `report.json` and `report.md`.

Import-only is cheap and validates format parsing; deployment downloads every
mod and needs a clean game directory (`<cache>/work/<id>/game`).

Offline rehearsal (not the clean-VM sign-off):

```sh
tests/pack-corpus/offline-check.sh <staging>/import.json /tmp/game build/bmcd
```

It runs deploy and verify inside `bwrap --unshare-net`, after the staging tree
has been populated by an online import. Requires `bwrap`. The 1.4.4
acceptance run must use `--stages import,deploy,export,verify` on a clean VM
with networking disabled **after** the first full download.

A second, VM-level replay is documented in `report-offline-vm.md`: both NICs
were link-down and deploy/verify still passed 108/108. That VM was reused and
its app came from a host-built portable bundle, so it is supporting evidence,
not the clean-VM sign-off.

## Acceptance status

Run on 2026-10-22 (Linux, current workspace):

- Modrinth: **20/20** import; **1/1** full `import -> deploy -> export -> verify`
  sample (`Fabulously Optimized`, 51 files, verify missing 0/corrupt 0).
- FTB: **20/20** import from live metadata; **0/20** full deploy completed.
  `dist.modpacks.ch` measured around 6 KB/s and the first deploy was still
  running after 10 minutes. The entry stays in the report, not removed.
- CurseForge: **0/20**, no `CURSEFORGE_API_KEY` in this workspace.

The full FTB run exposed a real importer bug: FTB API URLs contain raw spaces
(e.g. `FTB Academy-1.1.0`). `firstDownload()` now normalizes them with
`QUrl::toString(QUrl::FullyEncoded)`; `tests/tst_pack.cpp` covers it.

1.4.3 is complete. 1.4.4 is not signed off yet because these are external
blockers, not missing code:

- CurseForge API key is not available in this workspace, so the 20-pack CF
  corpus cannot be fetched or resolved.
- FTB does not publish an official pack zip; its corpus is metadata-generated.
- A clean freshly-provisioned offline VM is still pending. A reused Ubuntu
  22.04 VM did pass deploy+verify with both NICs down (108/108), but import
  was not run cold inside it and the app was a host-built portable bundle; see
  `report-offline-vm.md`.
- FTB CDN throughput makes 20 full deployments impractical from this network;
  run the corpus from a machine with normal CDN bandwidth (or a cached mirror).
