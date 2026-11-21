#!/usr/bin/env python3
"""Compare completed GPU command-buffer timestamp intervals, Direct vs CPU MDI.

These are entire Metal command-buffer GPU timeline durations, NOT isolated
model passes, GPU utilization, frame pacing, FPS or CPU render() timings.
"""
import csv
import math
import pathlib
import statistics
import sys


def percentile(values, p):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p * len(ordered)) - 1)]


def load_gpu(path):
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        if reader.fieldnames != ["frame", "gpu_command_buffer_ms", "valid"]:
            raise ValueError(f"{path}: unexpected GPU CSV columns")
        rows = list(reader)
    if [int(r["frame"]) for r in rows] != list(range(115, 181)):
        raise ValueError(f"{path}: missing/repeated GPU tags 115..180")
    values = []
    for r in rows:
        t = float(r["gpu_command_buffer_ms"])
        if int(r["valid"]) != 1 or not math.isfinite(t) or t <= 0:
            raise ValueError(f"{path}: invalid GPU completion at frame {r['frame']}")
        values.append(t)
    return values


def load_cpu(path, mode, items):
    with path.open(newline="") as fp:
        rows = list(csv.DictReader(fp))
    if [int(r["frame"]) for r in rows] != list(range(90, 196)):
        raise ValueError(f"{path}: unexpected CPU timeline frames")
    rows = rows[25:]
    if len(rows) != 81:
        raise ValueError(f"{path}: expected 81 measured frames")
    for r in rows:
        submissions = int(r["mdi_submissions"])
        commands = int(r["mdi_commands"])
        if mode == "direct" and (submissions != 0 or commands != 0):
            raise ValueError(f"{path}: Direct used indirect commands")
        if mode == "mdi" and (submissions < 1 or not (items <= commands <= 2 * items)):
            raise ValueError(f"{path}: MDI was missing/undersized ({submissions}, {commands})")
    times = [float(r["render_cpu_ms"]) for r in rows]
    if not all(math.isfinite(t) and t >= 0 for t in times):
        raise ValueError(f"{path}: invalid CPU render time")
    return times


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: batching-stage108-analyze.py CAPTURE_DIR")
    folder = pathlib.Path(sys.argv[1])
    result = []
    for items in (2048, 4096):
        for mode in ("direct", "mdi"):
            prefix = folder / f"{items}-{mode}"
            gpu = load_gpu(pathlib.Path(str(prefix) + "-gpu-frames.csv"))
            cpu = load_cpu(pathlib.Path(str(prefix) + "-frames.csv"), mode, items)
            result.append({
                "items": items, "mode": mode, "gpu_samples": len(gpu),
                "gpu_cb_median_ms": statistics.median(gpu),
                "gpu_cb_p95_ms": percentile(gpu, .95),
                "gpu_cb_max_ms": max(gpu),
                "cpu_render_median_ms": statistics.median(cpu),
                "cpu_render_p95_ms": percentile(cpu, .95)
            })
    for items in (2048, 4096):
        direct, mdi = [r for r in result if r["items"] == items]
        pct = (mdi["gpu_cb_median_ms"] / direct["gpu_cb_median_ms"] - 1) * 100
        print(f"{items}: Direct GPU CB median={direct['gpu_cb_median_ms']:.4f}ms "
              f"MDI GPU CB median={mdi['gpu_cb_median_ms']:.4f}ms "
              f"change={pct:+.2f}%")
    target = folder / "stage108-summary.csv"
    with target.open("w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=list(result[0]))
        writer.writeheader()
        writer.writerows(result)
    print(f"Report: {target}")
    print("CAVEAT: GPU command-buffer timestamps include whole-frame queue work, "
          "not just renderModels; CPU render() measurements are independent.")


if __name__ == "__main__":
    main()
