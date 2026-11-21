#!/usr/bin/env python3
"""Stage 129: compare measured Direct/MDI Metal GPU CB time across mesh tessellation.

The per-workload Stage127 analyzer validates all 8 real processes, topology,
CPU timelines and GPU timestamps. This analyzer checks its summaries, then
reports interaction between mesh workload and ICB pipeline inheritance.
Neither analyzer identifies isolated vertex shader or model-pass GPU time.
"""
import csv
import math
from pathlib import Path
import sys
import tempfile


def read_summary(root, subdivisions):
    path = root / f"mesh{subdivisions}" / "stage127-comparison.csv"
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 2 or {r["quality"] for r in rows} != {"off", "on"}:
        raise ValueError(f"{path}: expected exactly OFF/ON summaries")
    values = {}
    for row in rows:
        if (int(row["items"]), int(row["mdi_submissions"]),
                int(row["mdi_commands"])) != (2048, 18, 4086):
            raise ValueError(f"{path}: MDI topology changed: {row}")
        for field in ("gpu_direct_ms", "gpu_mdi_ms", "gpu_mdi_premium_ms",
                      "cpu_direct_ms", "cpu_mdi_ms"):
            number = float(row[field])
            if not math.isfinite(number):
                raise ValueError(f"{path}: invalid {field}")
            row[field] = number
        if min(row["gpu_direct_ms"], row["gpu_mdi_ms"],
               row["cpu_direct_ms"], row["cpu_mdi_ms"]) <= 0:
            raise ValueError(f"{path}: nonpositive frame time")
        if abs((row["gpu_mdi_ms"] - row["gpu_direct_ms"]) -
               row["gpu_mdi_premium_ms"]) > 1e-7:
            raise ValueError(f"{path}: inconsistent MDI GPU premium")
        values[row["quality"]] = row
    return values


def analyze(root, write=True):
    by_subdivision = {subdivisions: read_summary(root, subdivisions)
                      for subdivisions in (1, 4)}
    summaries = []
    scaling = []
    for subdivisions in (1, 4):
        for inherit in ("off", "on"):
            record = by_subdivision[subdivisions][inherit]
            summaries.append(dict(
                mesh_subdivisions=subdivisions,
                triangles_per_primitive=subdivisions ** 2,
                indirect_commands=4086,
                mdi_submissions=18,
                inherited_pipeline=inherit,
                gpu_direct_ms=record["gpu_direct_ms"],
                gpu_mdi_ms=record["gpu_mdi_ms"],
                gpu_mdi_premium_ms=record["gpu_mdi_premium_ms"],
                gpu_mdi_change_pct=100 * (
                    record["gpu_mdi_ms"] / record["gpu_direct_ms"] - 1),
                cpu_direct_ms=record["cpu_direct_ms"],
                cpu_mdi_ms=record["cpu_mdi_ms"]))
    for inherit in ("off", "on"):
        low = by_subdivision[1][inherit]
        high = by_subdivision[4][inherit]
        scaling.append(dict(
            inherited_pipeline=inherit,
            direct_gpu_delta_ms=high["gpu_direct_ms"] - low["gpu_direct_ms"],
            mdi_gpu_delta_ms=high["gpu_mdi_ms"] - low["gpu_mdi_ms"],
            mdi_premium_delta_ms=(high["gpu_mdi_premium_ms"] -
                                   low["gpu_mdi_premium_ms"])))
    if write:
        for filename, records in (
            ("stage129-summary.csv", summaries),
            ("stage129-scaling.csv", scaling),
        ):
            path = root / filename
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(records[0]))
                writer.writeheader()
                writer.writerows(records)
            print(f"Report: {path}")
        for row in summaries:
            print(f"mesh={row['mesh_subdivisions']} pipeline={row['inherited_pipeline']} "
                  f"Direct={row['gpu_direct_ms']:.6f}ms "
                  f"MDI={row['gpu_mdi_ms']:.6f}ms "
                  f"premium={row['gpu_mdi_premium_ms']:+.6f}ms "
                  f"({row['gpu_mdi_change_pct']:+.2f}%)")
        for row in scaling:
            print(f"SCALING pipeline={row['inherited_pipeline']}: "
                  f"Direct {row['direct_gpu_delta_ms']:+.6f}ms; "
                  f"MDI {row['mdi_gpu_delta_ms']:+.6f}ms; "
                  f"MDI premium {row['mdi_premium_delta_ms']:+.6f}ms")
        print("GPU timings are whole completed Metal command buffers, not "
              "isolated vertex shader or model-pass times.")
        print("Tessellation changes geometry work and rasterization; it does "
              "not isolate vertex ALU or represent game content.")
    return summaries, scaling


def self_test():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        for subdivisions, samples in (
            (1, ((.570, .820), (.569, .780))),
            (4, ((.800, 1.150), (.790, 1.090))),
        ):
            directory = root / f"mesh{subdivisions}"
            directory.mkdir()
            with (directory / "stage127-comparison.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=[
                    "quality", "items", "mdi_submissions", "mdi_commands",
                    "gpu_direct_ms", "gpu_mdi_ms", "gpu_mdi_premium_ms",
                    "cpu_direct_ms", "cpu_mdi_ms"])
                writer.writeheader()
                for inherit, (direct, mdi) in zip(("off", "on"), samples):
                    writer.writerow(dict(
                        quality=inherit, items=2048, mdi_submissions=18,
                        mdi_commands=4086, gpu_direct_ms=direct, gpu_mdi_ms=mdi,
                        gpu_mdi_premium_ms=mdi-direct, cpu_direct_ms=2.6,
                        cpu_mdi_ms=1.5))
        summaries, scaling = analyze(root, write=False)
        assert len(summaries) == 4 and len(scaling) == 2
        assert abs(scaling[0]["mdi_premium_delta_ms"] - .100) < 1e-9
        assert abs(scaling[1]["mdi_premium_delta_ms"] - .089) < 1e-9
        print("PASS: Stage129 analyzer synthetic self-test")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 2:
        analyze(Path(sys.argv[1]))
    else:
        sys.exit("Usage: batching-stage129-analyze.py CAPTURE_DIR | --self-test")
