#!/usr/bin/env python3
"""Build the 1.4.4 pack corpus manifest.

Modrinth archives are downloaded by ``run.py`` from the URL recorded here.
FTB's public API does not serve a zip: this script wraps the real version
metadata (targets + every file URL/SHA-1) in a minimal ``manifest.json`` zip so
the importer's FTB branch can still be exercised against live data. CurseForge
requires an API key; without one the format is skipped instead of faked.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

UA = "BMCD/1.4 corpus (https://github.com/bmcd)"
ROOT = Path(__file__).resolve().parent
DEFAULT_CACHE = Path(os.environ.get("BMCD_CORPUS_CACHE", Path.home() / ".cache" / "bmcd-pack-corpus"))


def get_json(url: str, headers: dict[str, str] | None = None):
    request = urllib.request.Request(url, headers={"User-Agent": UA, **(headers or {})})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def loader_from_dependencies(dependencies, loaders) -> tuple[str, str]:
    mapping = {
        "fabric-loader": "fabric",
        "quilt-loader": "quilt",
        "forge": "forge",
        "neoforge": "neoforge",
        "fabric": "fabric",
        "quilt": "quilt",
        "neo-forge": "neoforge",
        "neoforge": "neoforge",
    }
    if isinstance(dependencies, dict):
        for key, loader in mapping.items():
            value = dependencies.get(key)
            if isinstance(value, str) and value:
                return loader, value
    for name in loaders or []:
        loader = mapping.get(str(name).lower())
        if loader:
            return loader, ""
    return "", ""


def modrinth_entries(count: int) -> list[dict]:
    facets = json.dumps([["project_type:modpack"]])
    query = urllib.parse.urlencode({"limit": max(count * 2, 30), "index": "downloads", "facets": facets})
    search = get_json(f"https://api.modrinth.com/v2/search?{query}")
    entries: list[dict] = []
    for hit in search.get("hits", []):
        if len(entries) >= count:
            break
        project_id = hit.get("project_id")
        if not project_id:
            continue
        versions = get_json(f"https://api.modrinth.com/v2/project/{project_id}/version")
        chosen = None
        for version in versions:
            files = [item for item in version.get("files", []) if item.get("filename", "").endswith(".mrpack")]
            primary = next((item for item in files if item.get("primary")), files[0] if files else None)
            if primary and version.get("version_type") == "release":
                chosen = (version, primary)
                break
        if chosen is None:
            continue
        version, file = chosen
        dependencies = version.get("dependencies", {})
        loader, loader_version = loader_from_dependencies(dependencies, version.get("loaders", []))
        entries.append({
            "id": f"modrinth:{hit.get('slug', project_id)}:{version.get('id')}",
            "format": "modrinth",
            "name": hit.get("title", project_id),
            "url": file.get("url"),
            "sha1": file.get("hashes", {}).get("sha1", ""),
            "size": file.get("size", 0),
            "expected": {
                "minecraft": (version.get("game_versions") or [""])[0],
                "loader": loader,
                "loaderVersion": loader_version,
            },
            "source": f"https://modrinth.com/modpack/{hit.get('slug', project_id)}/version/{version.get('id')}",
        })
    if len(entries) < count:
        print(f"warning: Modrinth returned {len(entries)}/{count} usable packs", file=sys.stderr)
    return entries


def ftb_zip(detail: dict, version: dict, archive: Path) -> dict:
    files = []
    for item in version.get("files", []):
        url = item.get("url") or next(iter(item.get("downloads") or []), "")
        if not url:
            continue
        client = "unsupported" if item.get("serveronly") else "optional" if item.get("optional") else "required"
        files.append({
            "path": str(item.get("path", "")).removeprefix("./"),
            "downloads": [url],
            "hashes": [{"algo": 1, "value": item.get("sha1", "")}],
            "fileSize": item.get("size", 0),
            "env": {"client": client},
        })
    targets = []
    for target in version.get("targets", []):
        targets.append({"type": target.get("type"), "name": target.get("name"),
                        "version": target.get("version")})
    manifest = {
        "name": detail.get("name", ""),
        "version": version.get("name", ""),
        "author": "FTB",
        "targets": targets,
        "files": files,
    }
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False))
        bundle.writestr("overrides/.keep", "")
    return manifest


def ftb_entries(cache: Path, count: int) -> list[dict]:
    search = get_json("https://api.modpacks.ch/public/modpack/search/20?term=ftb")
    entries: list[dict] = []
    for pack_id in search.get("packs", []):
        if len(entries) >= count:
            break
        detail = get_json(f"https://api.modpacks.ch/public/modpack/{pack_id}")
        versions = [item for item in detail.get("versions", []) if item.get("type") == "release"]
        if not versions:
            continue
        version = versions[0]
        version_data = get_json(f"https://api.modpacks.ch/public/modpack/{pack_id}/{version.get('id')}")
        archive_name = f"ftb-{pack_id}-{version.get('id')}.zip"
        archive = cache / "archives" / archive_name
        manifest = ftb_zip(detail, version_data, archive)
        expected = {"minecraft": "", "loader": "", "loaderVersion": ""}
        for target in manifest.get("targets", []):
            if target.get("type") == "game":
                expected["minecraft"] = target.get("version", "")
            elif target.get("type") == "modloader":
                expected["loader"] = target.get("name", "").lower()
                expected["loaderVersion"] = target.get("version", "")
        entries.append({
            "id": f"ftb:{pack_id}:{version.get('id')}",
            "format": "ftb",
            "name": detail.get("name", f"FTB {pack_id}"),
            "archive": archive_name,
            "sha1": sha1_file(archive),
            "size": archive.stat().st_size,
            "expected": expected,
            "source": f"https://api.modpacks.ch/public/modpack/{pack_id}/{version.get('id')}",
            "generatedContainer": True,
        })
    return entries


def curseforge_entries(count: int, key: str) -> list[dict]:
    query = urllib.parse.urlencode({"gameId": 432, "classId": 4471, "pageSize": count, "sortField": 2})
    data = get_json(f"https://api.curseforge.com/v1/mods/search?{query}", {"x-api-key": key})
    entries = []
    for mod in data.get("data", []):
        latest = mod.get("latestFiles") or []
        file = next((item for item in latest if item.get("downloadUrl")), None)
        if file is None:
            continue
        game_versions = file.get("gameVersions", [])
        minecraft = game_versions[0] if game_versions else ""
        loader = ""
        for value in game_versions[1:]:
            lowered = value.lower()
            if lowered in ("forge", "fabric", "quilt", "neoforge"):
                loader = lowered
                break
        entries.append({
            "id": f"curseforge:{mod.get('slug', mod.get('id'))}:{file.get('id')}",
            "format": "curseforge",
            "name": mod.get("name", ""),
            "url": file.get("downloadUrl"),
            "sha1": next((item.get("value") for item in file.get("hashes", [])
                          if item.get("algo") == 1), ""),
            "size": file.get("fileLength", 0),
            "expected": {"minecraft": minecraft, "loader": loader, "loaderVersion": ""},
            "source": mod.get("links", {}).get("websiteUrl", ""),
        })
    return entries[:count]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--formats", default="modrinth,ftb",
                        help="comma separated: modrinth,ftb,curseforge")
    parser.add_argument("--count", type=int, default=20, help="packs per format")
    parser.add_argument("--out", type=Path, default=ROOT / "manifest.json")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    formats = [item.strip().lower() for item in args.formats.split(",") if item.strip()]
    entries: list[dict] = []
    if "modrinth" in formats:
        entries += modrinth_entries(args.count)
    if "ftb" in formats:
        entries += ftb_entries(args.cache, args.count)
    if "curseforge" in formats:
        key = os.environ.get("CURSEFORGE_API_KEY", "")
        if not key:
            print("error: CURSEFORGE_API_KEY is required for the CurseForge corpus", file=sys.stderr)
            return 2
        entries += curseforge_entries(args.count, key)
    payload = {
        "schema": 1,
        "generated": datetime.now(timezone.utc).isoformat(),
        "entries": entries,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(entries)} entries -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
