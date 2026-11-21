#!/usr/bin/env python3
"""Stage 121: compare Metal GPU whole-command-buffer timings across CPU MDI group caps.

No pass-only GPU timings, no FPS, and not a hardware ICB counter. Each
run is its own process; opposite ordering and Direct controls limit drift
but do not remove device/environment confounding.
"""
import csv
import math
import pathlib
import statistics
import sys

ORDER = ("direct1", "cap32a", "cap64a", "cap128a", "defaulta",
         "defaultb", "cap128b", "cap64b", "cap32b", "direct2")


def percentile(values, q):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered)*q)-1)]


def sample(path, fields, expected):
    with path.open(newline="") as file:
        reader = csv.DictReader(file)
        if not all(f in (reader.fieldnames or ()) for f in fields):
            raise ValueError(f"{path}: missing columns")
        rows = list(reader)
    if [int(r["frame"]) for r in rows] != list(expected):
        raise ValueError(f"{path}: unexpected frame sequence")
    return rows


def analyze(folder):
    rows = []
    for label in ORDER:
        mode = "direct" if label.startswith("direct") else "mdi"
        cap = 0 if mode == "direct" or label.startswith("default") else int(label[3:-1])
        prefix = folder / f"2048-{label}"
        gpu = sample(pathlib.Path(str(prefix)+"-gpu-frames.csv"),
                     ("frame", "gpu_command_buffer_ms", "valid"), range(115,181))
        cpu = sample(pathlib.Path(str(prefix)+"-frames.csv"),
                     ("frame", "render_cpu_ms", "mdi_submissions", "mdi_commands"),
                     range(90,196))[25:]
        times = [float(r["gpu_command_buffer_ms"]) for r in gpu]
        if any(int(r["valid"])!=1 for r in gpu) or any(not math.isfinite(v) or v<=0 for v in times):
            raise ValueError(f"{prefix}: invalid GPU completion")
        wall = [float(r["render_cpu_ms"]) for r in cpu]
        if any(not math.isfinite(v) or v<=0 for v in wall):
            raise ValueError(f"{prefix}: invalid CPU wall")
        subs = [int(r["mdi_submissions"]) for r in cpu]
        commands = [int(r["mdi_commands"]) for r in cpu]
        if mode == "direct":
            if any(subs) or any(commands):
                raise ValueError(f"{prefix}: direct submitted MDI")
        elif any(x<=0 for x in subs) or any(not (0.9*4096 <= c <= 4096) for c in commands):
            raise ValueError(f"{prefix}: MDI missing or too few commands")
        if len(set(subs)) != 1 or len(set(commands)) != 1:
            raise ValueError(f"{prefix}: workload changing in measured frames")
        row = dict(label=label, mode=mode, cap=cap, gpu_samples=len(times),
                   gpu_median_ms=statistics.median(times),
                   gpu_p95_ms=percentile(times,.95), gpu_max_ms=max(times),
                   cpu_median_ms=statistics.median(wall),
                   cpu_p95_ms=percentile(wall,.95),
                   mdi_submissions=subs[0], mdi_commands=commands[0])
        rows.append(row)
        print(f"{label:9} cap={cap:3} GPU={row['gpu_median_ms']:.5f}ms "
              f"p95={row['gpu_p95_ms']:.5f}ms CPU={row['cpu_median_ms']:.5f}ms "
              f"submissions={subs[0]} commands={commands[0]}")
    direct = statistics.median(r["gpu_median_ms"] for r in rows if r["mode"]=="direct")
    print(f"Direct GPU median-of-medians: {direct:.5f} ms")
    for cap in (32,64,128,0):
        relevant = [r for r in rows if r["mode"]=="mdi" and r["cap"]==cap]
        if len(relevant)!=2: raise ValueError(f"missing duplicate samples for cap={cap}")
        gpu = statistics.median(r["gpu_median_ms"] for r in relevant)
        cpu = statistics.median(r["cpu_median_ms"] for r in relevant)
        print(f"cap={cap:3} GPU={gpu:.5f}ms vs Direct {100*(gpu/direct-1):+.2f}% "
              f"CPU={cpu:.5f}ms")
    report = folder/"stage121-summary.csv"
    with report.open("w",newline="") as fp:
        writer = csv.DictWriter(fp,fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Report: {report}")
    print("NOTE: entire Metal GPU command-buffer time, not GPU model-only timing.")


if __name__=="__main__":
    if len(sys.argv)!=2:
        sys.exit("Usage: batching-stage121-analyze.py CAPTURE_DIR")
    analyze(pathlib.Path(sys.argv[1]))
