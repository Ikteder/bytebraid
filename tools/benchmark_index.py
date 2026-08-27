#!/usr/bin/env python3
"""Compare ByteBraid index backends on a deterministic synthetic tree."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import random
import statistics
import subprocess
import tempfile
import time


def build_corpus(root: Path, file_count: int, file_bytes: int) -> None:
    root.mkdir()
    family_size = 8
    for family_start in range(0, file_count, family_size):
        family = family_start // family_size
        base = random.Random(20260827 + family).randbytes(file_bytes)
        for offset in range(min(family_size, file_count - family_start)):
            content = bytearray(base)
            if offset >= 2:
                patch_size = min(4096, file_bytes // 8)
                patch_start = ((offset - 2) * 32768) % max(1, file_bytes - patch_size)
                content[patch_start : patch_start + patch_size] = random.Random(
                    9000 + family * family_size + offset
                ).randbytes(patch_size)
            (root / f"family-{family:03d}-v{offset}.bin").write_bytes(content)


def run_backend(binary: Path, corpus: Path, temp_parent: Path, backend: str, repetitions: int) -> dict:
    durations: list[float] = []
    report: dict | None = None
    for repetition in range(repetitions):
        report_path = temp_parent / f"{backend}-{repetition}.json"
        command = [
            str(binary), "scan", str(corpus),
            "--min-size", "1024", "--similarity", "0.65",
            "--chunk-min", "1024", "--chunk-average", "4096", "--chunk-max", "16384",
            "--index-backend", backend, "--index-partitions", "64",
            "--temp-dir", str(temp_parent), "--json", str(report_path),
        ]
        started = time.perf_counter()
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        durations.append(time.perf_counter() - started)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        leftovers = [path.name for path in temp_parent.glob("bytebraid-index-*")]
        if leftovers:
            raise RuntimeError(f"temporary indexes were not cleaned: {leftovers}")
    assert report is not None
    summary = report["summary"]
    return {
        "median_seconds": statistics.median(durations),
        "samples_seconds": durations,
        "posting_records": summary["index_posting_records"],
        "peak_resident_records": summary["index_peak_resident_records"],
        "partitions_used": summary["index_partitions_used"],
        "exact_groups": summary["exact_groups"],
        "near_pairs": summary["near_pairs"],
        "reclaimable_exact_bytes": summary["reclaimable_exact_bytes"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--files", type=int, default=128)
    parser.add_argument("--file-bytes", type=int, default=256 * 1024)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.files < 8 or args.file_bytes < 8192 or args.repetitions < 1:
        raise SystemExit("files >= 8, file-bytes >= 8192, and repetitions >= 1 are required")
    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="bytebraid-benchmark-") as temporary:
        temporary_path = Path(temporary)
        corpus = temporary_path / "corpus"
        spills = temporary_path / "spills"
        spills.mkdir()
        build_corpus(corpus, args.files, args.file_bytes)
        results = {
            backend: run_backend(binary, corpus, spills, backend, args.repetitions)
            for backend in ("memory", "disk")
        }

    comparable = ("exact_groups", "near_pairs", "reclaimable_exact_bytes", "posting_records")
    if any(results["memory"][key] != results["disk"][key] for key in comparable):
        raise RuntimeError("index backends produced different evidence")
    memory_peak = results["memory"]["peak_resident_records"]
    disk_peak = results["disk"]["peak_resident_records"]
    payload = {
        "measurement": "CLI wall time and algorithm-reported peak resident posting records; not process RSS or bytes",
        "environment": {"platform": platform.platform(), "python": platform.python_version()},
        "corpus": {
            "files": args.files,
            "file_bytes": args.file_bytes,
            "total_bytes": args.files * args.file_bytes,
            "seed": 20260827,
        },
        "results": results,
        "disk_peak_fraction": (disk_peak / memory_peak) if memory_peak else 0.0,
    }
    rendered = json.dumps(payload, indent=2) + "\n"
    print(rendered, end="")
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")


if __name__ == "__main__":
    main()
