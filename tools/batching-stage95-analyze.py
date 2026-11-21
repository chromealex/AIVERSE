#!/usr/bin/env python3
"""Compare CPU wall vs calling-thread CPU time in AIVERSE 1k MDI timelines.

CPU-only diagnostic: offcpu=wall-thread can include scheduler descheduling,
blocking syscalls, lock waits and driver activity. It cannot prove GPU waits.
"""
import csv
import math
import pathlib
import statistics
import sys

REQUIRED = ("frame", "render_cpu_ms", "mdi_submissions", "mdi_commands",
            "shadow_backend_ms", "opaque_backend_ms", "render_thread_cpu_ms",
            "render_offcpu_ms")
OPTIONAL_CLOCKS = ("render_process_cpu_ms", "render_mach_thread_cpu_ms")


def percentile(values, percent):
    sorted_values = sorted(values)
    return sorted_values[max(0, math.ceil(len(sorted_values) * percent) - 1)]


def analyze(path, threshold=1.2):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not all(field in (reader.fieldnames or []) for field in REQUIRED):
            raise ValueError(f"{path}: missing columns: {set(REQUIRED) - set(reader.fieldnames or [])}")
        available = [key for key in OPTIONAL_CLOCKS if key in (reader.fieldnames or [])]
        records = [{key: float(row[key]) for key in REQUIRED + tuple(available)}
                   for row in reader]
    if [int(x["frame"]) for x in records] != list(range(90, 196)):
        raise ValueError(f"{path}: expected unique ordered frames 90..195")
    for r in records:
        if any(not math.isfinite(r[k]) or r[k] < 0 for k in REQUIRED):
            raise ValueError(f"{path}: invalid/unsupported thread CPU sample, frame={r['frame']}")
        if r["render_thread_cpu_ms"] > r["render_cpu_ms"] + 0.15:
            raise ValueError(f"{path}: thread time unexpectedly exceeds wall time, frame={r['frame']}")
        if "render_process_cpu_ms" in r and r["render_process_cpu_ms"] < r["render_thread_cpu_ms"] - 0.15:
            raise ValueError(f"{path}: process CPU unexpectedly below thread CPU, frame={r['frame']}")
        if "render_mach_thread_cpu_ms" in r:
            if r["render_mach_thread_cpu_ms"] < 0:
                raise ValueError(f"{path}: unavailable Mach CPU clock at frame={r['frame']}")
            # The Mach clock is independently validated with a CPU busy-loop.
            # Do not rely on the raw POSIX offcpu CSV column when they disagree.
            r["derived_offcpu_ms"] = max(0.0, r["render_cpu_ms"] -
                                         r["render_mach_thread_cpu_ms"])
            r["trusted_thread_cpu_ms"] = r["render_mach_thread_cpu_ms"]
        else:
            r["derived_offcpu_ms"] = r["render_offcpu_ms"]
            r["trusted_thread_cpu_ms"] = r["render_thread_cpu_ms"]
    measured = [r for r in records if 115 <= r["frame"] <= 195 and r["mdi_submissions"] > 0]
    if len(measured) < 40:
        raise ValueError(f"{path}: need at least 40 MDI measured frames")
    slow = [r for r in measured if r["render_cpu_ms"] > threshold]
    fast = [r for r in measured if r["render_cpu_ms"] <= threshold]
    def med(items, fn):
        return statistics.median(fn(r) for r in items) if items else float("nan")
    def backend(r):
        return r["shadow_backend_ms"] + r["opaque_backend_ms"]
    def group(label, items):
        return {
            "file": path.name, "group": label, "frames": len(items),
            "wall_median_ms": med(items, lambda r: r["render_cpu_ms"]),
            "thread_median_ms": med(items, lambda r: r["trusted_thread_cpu_ms"]),
            "posix_thread_median_ms": med(items, lambda r: r["render_thread_cpu_ms"]),
            "offcpu_median_ms": med(items, lambda r: r["derived_offcpu_ms"]),
            "offcpu_p95_ms": (percentile([r["derived_offcpu_ms"] for r in items], .95)
                             if items else float("nan")),
            "backend_wall_median_ms": med(items, backend),
            "offcpu_fraction_median": med(items, lambda r:
                r["derived_offcpu_ms"] / r["render_cpu_ms"]
                if r["render_cpu_ms"] else 0.0),
            "process_cpu_median_ms": med(items, lambda r:
                r.get("render_process_cpu_ms", float("nan"))),
            "mach_thread_cpu_median_ms": med(items, lambda r:
                r.get("render_mach_thread_cpu_ms", float("nan"))),
        }
    return [group("all_mdi", measured), group("fast_mdi", fast),
            group("slow_mdi", slow)]


def main():
    if len(sys.argv) < 2:
        sys.exit("Usage: batching-stage95-analyze.py RUN1-frames.csv [RUN2-frames.csv ...]")
    paths = [pathlib.Path(name) for name in sys.argv[1:]]
    results = [entry for path in paths for entry in analyze(path)]
    columns = list(results[0])
    summary = paths[0].parent / "stage95-thread-summary.csv"
    with summary.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=columns)
        writer.writeheader()
        writer.writerows(results)
    print("Stage 95 CPU wall vs render-calling-thread time (no GPU timestamps)")
    print(f"{'Run':28} {'Group':9} {'n':>4} {'wall':>8} {'thread':>8} "
          f"{'offcpu':>8} {'offcpu p95':>10} {'backend wall':>13}"
          f" {'procCPU':>8} {'machThr':>8} {'POSIX':>8}")
    for r in results:
        print(f"{r['file'][:28]:28} {r['group']:9} {r['frames']:4d} "
              f"{r['wall_median_ms']:8.4f} {r['thread_median_ms']:8.4f} "
              f"{r['offcpu_median_ms']:8.4f} {r['offcpu_p95_ms']:10.4f} "
              f"{r['backend_wall_median_ms']:13.4f} "
              f"{r['process_cpu_median_ms']:8.4f} "
              f"{r['mach_thread_cpu_median_ms']:8.4f} "
              f"{r['posix_thread_median_ms']:8.4f}")
    print(f"Saved: {summary}")
    print("NOTE: offcpu recomputed from Mach thread CPU when available,")
    print("not from the old CSV offcpu column; POSIX remains a diagnostic.")
    print("No GPU timing or proof of scheduler waits.")


if __name__ == "__main__":
    main()
