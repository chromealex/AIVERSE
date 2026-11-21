#!/usr/bin/env python3
"""Stage 119: controlled one-model-pass stress, non-additive GPU CB comparisons."""
import csv
import math
import pathlib
import statistics
import sys


def measured(path, kind):
    with path.open(newline="") as source:
        reader = csv.DictReader(source)
        rows = list(reader)
    first, last = (115, 180) if kind == "gpu" else (90, 195)
    if [int(row["frame"]) for row in rows] != list(range(first, last + 1)):
        raise ValueError(f"{path}: frame sequence differs from {first}..{last}")
    if kind == "gpu":
        if any(int(row["valid"]) != 1 for row in rows):
            raise ValueError(f"{path}: invalid GPU timestamp")
        values = [float(row["gpu_command_buffer_ms"]) for row in rows]
    else:
        rows = rows[25:]
        values = [float(row["render_cpu_ms"]) for row in rows]
    if any(not math.isfinite(x) or x <= 0 for x in values):
        raise ValueError(f"{path}: invalid/non-positive timings")
    return statistics.median(values), sorted(values)[math.ceil(.95 * len(values)) - 1]


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: batching-stage119-analyze.py CAPTURE_DIR")
    folder = pathlib.Path(sys.argv[1])
    data = []
    for phase in ("full", "shadow", "opaque"):
        for mode in ("direct", "mdi"):
            base = folder / f"2048-{phase}-{mode}"
            gpu = measured(pathlib.Path(str(base) + "-gpu-frames.csv"), "gpu")
            cpu = measured(pathlib.Path(str(base) + "-frames.csv"), "cpu")
            row = dict(phase=phase, mode=mode, gpu_cb_median_ms=gpu[0],
                       gpu_cb_p95_ms=gpu[1], cpu_render_median_ms=cpu[0],
                       cpu_render_p95_ms=cpu[1])
            data.append(row)
        direct, mdi = data[-2:]
        print(f"{phase:6} GPU CB Direct={direct['gpu_cb_median_ms']:.5f}ms "
              f"MDI={mdi['gpu_cb_median_ms']:.5f}ms "
              f"delta={(mdi['gpu_cb_median_ms']/direct['gpu_cb_median_ms']-1)*100:+.2f}%")
    output = folder / "stage119-summary.csv"
    with output.open("w", newline="") as dest:
        writer = csv.DictWriter(dest, fieldnames=list(data[0]))
        writer.writeheader()
        writer.writerows(data)
    print(f"Report: {output}")
    print("WARNING: Shadow-only/opaque-only are changed render workloads, "
          "not isolated counter timestamps. Never subtract to claim exact per-pass GPU time.")


if __name__ == "__main__":
    main()
