#!/usr/bin/env python3
"""Stage 123: matched MDI submission count, increasing indirect command count.

Each size has two Direct and two MDI *process* controls in D-M-M-D order.
Do not infer an exact marginal per-command GPU cost from changed scenes:
fragment/vertex work, buffer size and driver scheduling also vary.
"""
import csv
import math
import pathlib
import statistics
import sys

CONFIGS = ((1024, 32), (2048, 64), (4096, 128))
ORDER = ("direct1", "mdi1", "mdi2", "direct2")
MEASURED = range(115, 196)
GPU_FRAMES = range(115, 181)


def p95(values):
    return sorted(values)[math.ceil(.95 * len(values))-1]


def rows(path, required, expected):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not set(required).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: missing required columns")
        data = list(reader)
    if [int(item["frame"]) for item in data] != list(expected):
        raise ValueError(f"{path}: unexpected frame sequence")
    return data


def get_run(folder, items, cap, trial):
    mode = "mdi" if trial.startswith("mdi") else "direct"
    prefix = folder / f"{items}-{trial}"
    gpu = rows(pathlib.Path(str(prefix)+"-gpu-frames.csv"),
               ("frame", "gpu_command_buffer_ms", "valid"), GPU_FRAMES)
    cpu = rows(pathlib.Path(str(prefix)+"-frames.csv"),
               ("frame", "render_cpu_ms", "mdi_submissions", "mdi_commands",
                "shadow_draws", "opaque_draws"), range(90, 196))[25:]
    if [int(r["frame"]) for r in cpu] != list(MEASURED):
        raise ValueError(f"{prefix}: CPU measured frames invalid")
    gt = [float(x["gpu_command_buffer_ms"]) for x in gpu]
    ct = [float(x["render_cpu_ms"]) for x in cpu]
    if any(int(x["valid"]) != 1 for x in gpu) or not all(
        math.isfinite(x) and x > 0 for x in gt + ct
    ):
        raise ValueError(f"{prefix}: missing/invalid GPU/CPU timing")
    subs = [int(x["mdi_submissions"]) for x in cpu]
    commands = [int(x["mdi_commands"]) for x in cpu]
    shadow = [int(x["shadow_draws"]) for x in cpu]
    opaque = [int(x["opaque_draws"]) for x in cpu]
    if len(set(subs)) != 1 or len(set(commands)) != 1:
        raise ValueError(f"{prefix}: MDI counts change across frames")
    if mode == "direct":
        if any(subs) or any(commands) or any(s != items for s in shadow) or any(o != items for o in opaque):
            raise ValueError(f"{prefix}: invalid direct 2-pass workload")
    else:
        # 32 groups of exactly 'cap' commands in each pass; 64 submissions
        # across both model passes, with all authored commands submitted.
        if subs[0] != 64 or commands[0] != 2*items:
            raise ValueError(f"{prefix}: expected 64 submissions / {2*items} commands, "
                             f"got {subs[0]} / {commands[0]}")
        if any(s != 32 for s in shadow) or any(o != 32 for o in opaque):
            raise ValueError(f"{prefix}: expected 32 model draw groups per pass")
    return {
        "items": items, "cap": cap if mode == "mdi" else 0, "trial": trial,
        "mode": mode, "gpu_n": len(gt),
        "gpu_median_ms": statistics.median(gt), "gpu_p95_ms": p95(gt),
        "gpu_max_ms": max(gt),
        "cpu_median_ms": statistics.median(ct), "cpu_p95_ms": p95(ct),
        "mdi_submissions": subs[0], "mdi_commands": commands[0],
        "shadow_draws": shadow[0], "opaque_draws": opaque[0],
    }


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: batching-stage123-analyze.py CAPTURE_DIRECTORY")
    folder = pathlib.Path(sys.argv[1])
    results, comparisons = [], []
    print("Stage123 equal ICB submissions, varied indirect command count (CPU/GPU whole CB)")
    for items, cap in CONFIGS:
        runset = []
        for trial in ORDER:
            r = get_run(folder, items, cap, trial)
            results.append(r)
            runset.append(r)
            print(f"  {items} {trial:7} GPU={r['gpu_median_ms']:.5f}ms "
                  f"P95={r['gpu_p95_ms']:.5f} CPU={r['cpu_median_ms']:.5f}ms "
                  f"submissions={r['mdi_submissions']} commands={r['mdi_commands']}")
        direct = statistics.median(r["gpu_median_ms"] for r in runset if r["mode"]=="direct")
        mdi = statistics.median(r["gpu_median_ms"] for r in runset if r["mode"]=="mdi")
        cpu_direct = statistics.median(r["cpu_median_ms"] for r in runset if r["mode"]=="direct")
        cpu_mdi = statistics.median(r["cpu_median_ms"] for r in runset if r["mode"]=="mdi")
        premium = mdi-direct
        record = {
            "items": items, "group_cap": cap, "mdi_submissions": 64,
            "mdi_commands": 2*items,
            "gpu_direct_median_ms": direct, "gpu_mdi_median_ms": mdi,
            "gpu_mdi_premium_ms": premium,
            "gpu_mdi_change_pct": 100 * premium / direct,
            "premium_per_command_ms_descriptive_only": premium / (2*items),
            "cpu_direct_median_ms": cpu_direct, "cpu_mdi_median_ms": cpu_mdi,
            "cpu_mdi_change_pct": 100*(cpu_mdi/cpu_direct-1),
        }
        comparisons.append(record)
        print(f"  {items}: GPU Direct={direct:.5f}ms MDI={mdi:.5f}ms "
              f"premium={premium:.5f}ms ({record['gpu_mdi_change_pct']:+.2f}%), "
              f"CPU change={record['cpu_mdi_change_pct']:+.2f}%")
    for name, data in (("stage123-runs.csv", results), ("stage123-comparison.csv", comparisons)):
        with (folder/name).open("w",newline="") as target:
            writer=csv.DictWriter(target,fieldnames=list(data[0]))
            writer.writeheader()
            writer.writerows(data)
        print(f"Saved: {folder/name}")
    print("WARNING: even with 64 ICB submissions, different scenes change GPU workload; "
          "per-command premium is descriptive and NOT an isolated causal cost.")
    print("GPU times are full completed Metal command-buffer intervals, NOT model-pass-only or FPS.")


if __name__ == "__main__":
    main()
