#!/usr/bin/env python3
"""Run the real-pack corpus through import -> deploy -> export -> verify.

Archives stay in BMCD_CORPUS_CACHE (never in the repository). Every attempted
pack stays in the report's denominator: a failure is recorded, not skipped.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

UA = "BMCD/1.4 corpus (https://github.com/bmcd)"
ROOT = Path(__file__).resolve().parent
DEFAULT_CACHE = Path(os.environ.get("BMCD_CORPUS_CACHE", Path.home() / ".cache" / "bmcd-pack-corpus"))


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_name(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)[:120]


def ensure_archive(entry: dict, cache: Path) -> tuple[Path | None, str]:
    archive_dir = cache / "archives"
    archive_dir.mkdir(parents=True, exist_ok=True)
    if entry.get("archive"):
        path = archive_dir / Path(entry["archive"]).name
        if not path.is_file():
            return None, f"generated archive missing: {path}"
    elif entry.get("url"):
        suffix = ".mrpack" if entry.get("format") == "modrinth" else ".zip"
        path = archive_dir / (safe_name(entry["id"]) + suffix)
        if not path.is_file():
            request = urllib.request.Request(entry["url"], headers={"User-Agent": UA})
            with urllib.request.urlopen(request, timeout=120) as response, path.open("wb") as out:
                shutil.copyfileobj(response, out)
    else:
        return None, "no url or generated archive"
    expected = entry.get("sha1", "")
    if expected and sha1_file(path) != expected:
        return None, f"sha1 mismatch: {path.name}"
    return path, ""


def run(command: list[str], timeout: int) -> tuple[int, str]:
    try:
        proc = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout)
        return proc.returncode, proc.stdout
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return 124, output + f"\n[timeout after {timeout}s]"


def first_int(pattern: str, text: str) -> int:
    match = re.search(pattern, text, re.MULTILINE)
    return int(match.group(1)) if match else 0


def run_entry(entry: dict, args, cache: Path) -> dict:
    result = {
        "id": entry["id"], "format": entry.get("format"), "name": entry.get("name"),
        "expected": entry.get("expected", {}), "stages": {}, "ok": False,
    }
    work = args.work / safe_name(entry["id"])
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)
    game = work / "game"
    archive, error = ensure_archive(entry, cache)
    if archive is None:
        result["error"] = error
        return result

    import_output = ""
    if "import" in args.stages:
        # --import --to is the only CLI path that downloads every pack file and
        # then deploys it; --pack --to expects an already-populated cache.
        command = [args.binary, "--import", str(archive)]
        if "deploy" in args.stages:
            command += ["--to", str(game)]
        code, import_output = run(command, args.timeout)
        result["stages"]["import"] = {"ok": code == 0, "code": code, "output": import_output[-4000:]}
        match = re.search(r"^staging: (.+)$", import_output, re.MULTILINE)
        if match:
            pack = Path(match.group(1).strip()) / "import.json"
            result["pack"] = str(pack)
        result["files"] = first_int(r"^files: (\d+)", import_output)
        result["unresolved"] = len(re.findall(r"^warning: 无下载地址 ", import_output, re.MULTILINE))
        metadata = re.search(r"^minecraft: (\S+) / (\S+)(?: (\S+))?$", import_output, re.MULTILINE)
        if metadata:
            result["actual"] = {"minecraft": metadata.group(1), "loader": metadata.group(2),
                                "loaderVersion": metadata.group(3) or ""}
            expected = entry.get("expected", {})
            mismatches = []
            for key in ("minecraft", "loader", "loaderVersion"):
                wanted = expected.get(key, "")
                if wanted and result["actual"].get(key, "") != wanted:
                    mismatches.append(f"{key}: expected {wanted}, got {result['actual'].get(key, '')}")
            result["metadataMismatches"] = mismatches
            if mismatches:
                result["error"] = "metadata mismatch: " + "; ".join(mismatches)
                return result
        if code != 0:
            result["error"] = "import/deploy failed"
            if "deploy" in args.stages:
                result["stages"]["deploy"] = {"ok": False, "code": code,
                                              "output": import_output[-4000:]}
            return result
        if "deploy" in args.stages:
            result["stages"]["deploy"] = {"ok": True, "code": 0,
                                          "output": "included in --import --to"}
        if not result.get("pack"):
            result["error"] = "import did not report a staging directory"
            return result
        pack = Path(result["pack"])
        if "export" in args.stages:
            exported = work / "export.mrpack"
            code, output = run([args.binary, "--pack", str(pack), "--format", "mrpack",
                                "--out", str(exported)], args.timeout)
            result["stages"]["export"] = {"ok": code == 0 and exported.is_file(),
                                          "code": code, "output": output[-4000:]}
            if code != 0 or not exported.is_file():
                result["error"] = "export failed"
                return result
        if "verify" in args.stages:
            target = work / "game" if game.exists() else Path(entry.get("target", work / "game"))
            code, output = run([args.binary, "--pack", str(pack), "--verify", str(target)],
                               args.timeout)
            result["stages"]["verify"] = {"ok": code == 0, "code": code, "output": output[-4000:]}
            if code != 0:
                result["error"] = "verify failed"
                return result
    result["ok"] = all(stage.get("ok") for stage in result["stages"].values()) and bool(result["stages"])
    return result


def write_report(results: list[dict], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": 1,
        "generated": datetime.now(timezone.utc).isoformat(),
        "total": len(results),
        "passed": sum(1 for item in results if item.get("ok")),
        "results": results,
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# BMCD pack corpus report", "",
        f"- generated: `{payload['generated']}`",
        f"- passed: **{payload['passed']}/{payload['total']}**", "",
        "| # | format | pack | import | deploy | export | verify | result |",
        "|---:|---|---|---|---|---|---|---|",
    ]
    for index, item in enumerate(results, 1):
        stages = item.get("stages", {})
        cells = []
        for stage in ("import", "deploy", "export", "verify"):
            value = stages.get(stage)
            cells.append("—" if value is None else ("ok" if value.get("ok") else "FAIL"))
        outcome = "PASS" if item.get("ok") else (item.get("error") or "FAIL")
        lines.append(f"| {index} | {item.get('format', '')} | {item.get('name', '')} | "
                     + " | ".join(cells) + f" | {outcome} |")
    path.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "manifest.json")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--work", type=Path, default=None)
    parser.add_argument("--binary", default=str(ROOT.parent.parent / "build" / "bmcd"))
    parser.add_argument("--stages", default="import,deploy,export,verify")
    parser.add_argument("--format", action="append", default=[], help="only this pack format")
    parser.add_argument("--id", default="", help="only entries whose id contains this text")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--report", type=Path, default=ROOT / "report.json")
    args = parser.parse_args()
    args.stages = {item.strip() for item in args.stages.split(",") if item.strip()}
    args.work = args.work or args.cache / "work"
    data = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = data.get("entries", [])
    if args.format:
        entries = [item for item in entries if item.get("format") in args.format]
    if args.id:
        entries = [item for item in entries if args.id in item.get("id", "")]
    if args.limit > 0:
        entries = entries[:args.limit]
    if not entries:
        print("error: no corpus entries selected", file=sys.stderr)
        return 2
    binary = Path(args.binary)
    if not binary.is_file():
        print(f"error: binary not found: {binary}", file=sys.stderr)
        return 2
    results = []
    for index, entry in enumerate(entries, 1):
        print(f"[{index}/{len(entries)}] {entry.get('format')}: {entry.get('name')}", flush=True)
        result = run_entry(entry, args, args.cache)
        results.append(result)
        print(f"  {'PASS' if result.get('ok') else 'FAIL: ' + result.get('error', '')}", flush=True)
        write_report(results, args.report)
    write_report(results, args.report)
    passed = sum(1 for item in results if item.get("ok"))
    print(f"report: {args.report} ({passed}/{len(results)} passed)")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
