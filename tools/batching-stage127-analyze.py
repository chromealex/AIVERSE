#!/usr/bin/env python3
"""Stage 127: GPU/CPU comparison at fixed 2048 primitive workload, varying opt-in CPU ICB pipeline inheritance.

The fixture keeps material quality fixed while toggling CPU ICB parent
pipeline inheritance. Buffer bindings remain explicit in both modes.
"""
import csv
import math
from pathlib import Path
import statistics
import sys

ORDER = (
    ("off", "direct", 1), ("off", "mdi", 1),
    ("on", "direct", 1), ("on", "mdi", 1),
    ("on", "mdi", 2), ("on", "direct", 2),
    ("off", "mdi", 2), ("off", "direct", 2),
)


def read_frames(path, fields, frame_range):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not set(fields).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: missing columns {set(fields)-set(reader.fieldnames or [])}")
        rows = list(reader)
    if [int(r["frame"]) for r in rows] != list(frame_range):
        raise ValueError(f"{path}: wrong frame order/length")
    return rows


def p95(values):
    return sorted(values)[math.ceil(len(values)*.95)-1]


def run(folder, quality, mode, rep):
    name = f"2048-{quality}-{mode}{rep}"
    prefix = folder / name
    gpu = read_frames(Path(str(prefix)+"-gpu-frames.csv"),
                      ("frame", "gpu_command_buffer_ms", "valid"), range(115,181))
    cpu = read_frames(Path(str(prefix)+"-frames.csv"),
                      ("frame", "render_cpu_ms", "mdi_submissions", "mdi_commands",
                       "shadow_draws", "opaque_draws"), range(90,196))[25:]
    assert len(cpu)==81
    gpu_ms = [float(r["gpu_command_buffer_ms"]) for r in gpu]
    cpu_ms = [float(r["render_cpu_ms"]) for r in cpu]
    if not all(int(r["valid"])==1 for r in gpu) or not all(
        math.isfinite(x) and x>0 for x in gpu_ms+cpu_ms
    ):
        raise ValueError(f"{name}: incomplete or invalid GPU/CPU timeline")
    topology = {
        (int(r["mdi_submissions"]), int(r["mdi_commands"]),
         int(r["shadow_draws"]), int(r["opaque_draws"]))
        for r in cpu
    }
    if len(topology)!=1:
        raise ValueError(f"{name}: topology changed within measured frames: {topology}")
    submissions, commands, shadow, opaque = next(iter(topology))
    if mode=="direct":
        if (submissions,commands,shadow,opaque)!=(0,0,2048,2048):
            raise ValueError(f"{name}: unexpected Direct draw topology {topology}")
    else:
        if (submissions,commands,shadow,opaque)!=(18,4086,14,14):
            raise ValueError(f"{name}: unexpected MDI draw topology {topology}")
    return dict(quality=quality, mode=mode, repeat=rep, gpu_samples=len(gpu_ms),
                gpu_median_ms=statistics.median(gpu_ms),gpu_p95_ms=p95(gpu_ms),
                gpu_max_ms=max(gpu_ms),
                cpu_median_ms=statistics.median(cpu_ms),cpu_p95_ms=p95(cpu_ms),
                mdi_submissions=submissions,mdi_commands=commands,
                shadow_draws=shadow,opaque_draws=opaque)


def main():
    if len(sys.argv)!=2:
        sys.exit("Usage: batching-stage127-analyze.py CAPTURE_DIR")
    folder=Path(sys.argv[1])
    records = [run(folder,quality,mode,rep) for quality,mode,rep in ORDER]
    for r in records:
        print(f"{r['quality']:4} {r['mode']:6} rep={r['repeat']} "
              f"GPU median={r['gpu_median_ms']:.6f}ms p95={r['gpu_p95_ms']:.6f}ms "
              f"CPU median={r['cpu_median_ms']:.6f}ms "
              f"submissions={r['mdi_submissions']} commands={r['mdi_commands']}")
    summary = []
    for quality in ("off","on"):
        a = [r for r in records if r["quality"]==quality and r["mode"]=="direct"]
        b = [r for r in records if r["quality"]==quality and r["mode"]=="mdi"]
        if len(a)!=2 or len(b)!=2:
            raise ValueError(f"{quality}: unmatched mode controls")
        gd=statistics.median(r["gpu_median_ms"] for r in a)
        gm=statistics.median(r["gpu_median_ms"] for r in b)
        cd=statistics.median(r["cpu_median_ms"] for r in a)
        cm=statistics.median(r["cpu_median_ms"] for r in b)
        summary.append(dict(quality=quality,items=2048,
                            gpu_direct_ms=gd,gpu_mdi_ms=gm,
                            gpu_mdi_premium_ms=gm-gd,gpu_mdi_change_pct=100*(gm/gd-1),
                            cpu_direct_ms=cd,cpu_mdi_ms=cm,
                            cpu_mdi_change_pct=100*(cm/cd-1),
                            mdi_submissions=18,mdi_commands=4086))
        print(f"PIPELINE {quality}: GPU Direct={gd:.6f} MDI={gm:.6f} "
              f"premium={gm-gd:.6f}ms ({100*(gm/gd-1):+.2f}%), "
              f"CPU MDI change={100*(cm/cd-1):+.2f}%")
    for filename, values in (
        ("stage127-runs.csv",records), ("stage127-comparison.csv",summary)
    ):
        with (folder/filename).open("w",newline="") as file:
            writer=csv.DictWriter(file,fieldnames=list(values[0]))
            writer.writeheader()
            writer.writerows(values)
        print(f"Report: {folder/filename}")
    print("Note: GPU timings are whole completed Metal command buffers, not per-model pass.")
    print("Only the opt-in ICB pipeline inheritance mode differs, not shader quality.")


if __name__=="__main__":
    main()
