#!/usr/bin/env python3
"""Stage 86 CPU-only MDI/AUTO per-frame timeline analyzer (stdlib only)."""
import csv
import math
import pathlib
import statistics
import sys


COLUMNS = (
    "frame", "render_cpu_ms", "mdi_submissions", "mdi_commands",
    "shadow_draws", "opaque_draws", "shadow_cpu_ms", "opaque_cpu_ms",
    "shadow_backend_ms", "opaque_backend_ms"
)


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def load_csv(path):
    with path.open(newline="") as source:
        reader = csv.DictReader(source)
        if tuple(reader.fieldnames or ()) != COLUMNS:
            raise ValueError(f"{path}: unexpected CSV header: {reader.fieldnames}")
        rows = []
        for row in reader:
            converted = {key: (int(value) if key in
                ("frame", "mdi_submissions", "mdi_commands",
                 "shadow_draws", "opaque_draws") else float(value))
                for key, value in row.items()}
            if not all(math.isfinite(converted[key]) and converted[key] >= 0
                       for key in COLUMNS):
                raise ValueError(f"{path}: invalid value at frame {converted['frame']}")
            rows.append(converted)
    if [r["frame"] for r in rows] != list(range(90, 196)):
        raise ValueError(f"{path}: expected exactly frames 90..195, got {len(rows)} rows")
    return rows


def longest_slow_streak(rows, threshold=1.2):
    longest = current = 0
    for row in rows:
        if row["render_cpu_ms"] > threshold:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def slow_runs(rows, threshold=1.2):
    """Contiguous measured-frame periods above a CPU reference threshold."""
    result = []
    active = []
    for row in rows:
        if row["render_cpu_ms"] > threshold:
            active.append(row)
        elif active:
            result.append(active)
            active = []
    if active:
        result.append(active)
    return result


def stage_cost(row):
    # The two backend submit measurements are CPU sub-stages of render().
    # Do not call the remainder "GPU stall": it includes other CPU work.
    backend = row["shadow_backend_ms"] + row["opaque_backend_ms"]
    return backend, max(0.0, row["render_cpu_ms"] - backend)


def mode_name(row):
    return "mdi" if row["mdi_submissions"] else "direct"


def mode_slow_runs(rows, mode, threshold=1.2):
    """Contiguous slow spans of the SAME actual mode: other-mode frames split spans."""
    result, active = [], []
    for row in rows:
        if mode_name(row) == mode and row["render_cpu_ms"] > threshold:
            active.append(row)
        elif active:
            result.append(active)
            active = []
    if active:
        result.append(active)
    return sorted(result, key=lambda run: (-len(run), run[0]["frame"]))


def median_cost(group):
    """Median of row-wise paired costs, never the sum of independent medians."""
    if not group:
        return float("nan"), float("nan")
    costs = [stage_cost(r) for r in group]
    return (statistics.median(x[0] for x in costs),
            statistics.median(x[1] for x in costs))


def stats(label, rows, kind):
    measured = [r for r in rows if 115 <= r["frame"] <= 195]
    mdi = [r for r in measured if r["mdi_submissions"]]
    direct = [r for r in measured if not r["mdi_submissions"]]
    if kind == "mdi" and len(mdi) != 81:
        raise ValueError(f"{label}: forced MDI had {len(mdi)}/81 submitted frames")
    if kind == "auto" and not mdi:
        raise ValueError(f"{label}: AUTO did not produce any sampled MDI frames")
    late = [r for r in measured if 160 <= r["frame"] <= 195]
    def med(group, key):
        return statistics.median(r[key] for r in group) if group else float("nan")
    def p95(group, key):
        return percentile([r[key] for r in group], 0.95) if group else float("nan")
    slow = [r for r in mdi if r["frame"] >= 98 and r["render_cpu_ms"] > 1.2]
    # Report the backend contribution and remainder. These CPU stages are
    # instrumented inside renderModels and do not exhaust all render() costs.
    worst = max(measured, key=lambda r: r["render_cpu_ms"])
    episodes = slow_runs(measured)
    episodes.sort(key=lambda run: (-len(run), run[0]["frame"]))
    longest = episodes[0] if episodes else []
    slow_mdi = [r for r in slow if r["mdi_submissions"]]
    worst_backend, worst_other_cpu = stage_cost(worst)
    mdi_runs = mode_slow_runs(measured, "mdi")
    direct_runs = mode_slow_runs(measured, "direct")
    mdi_longest = mdi_runs[0] if mdi_runs else []
    direct_longest = direct_runs[0] if direct_runs else []
    mdi_cost, _ = median_cost(mdi)
    slow_mdi_cost, _ = median_cost(slow_mdi)
    longest_cost, longest_other = median_cost(longest)
    mdi_longest_cost, mdi_longest_other = median_cost(mdi_longest)
    direct_longest_cost, direct_longest_other = median_cost(direct_longest)
    return {
        "trial": label, "mode": kind,
        "measured_frames": len(measured),
        "mdi_frames": len(mdi), "direct_frames": len(direct),
        "mdi_submissions": sum(r["mdi_submissions"] for r in measured),
        "render_median_ms": med(measured, "render_cpu_ms"),
        "render_p95_ms": p95(measured, "render_cpu_ms"),
        "late_median_ms": med(late, "render_cpu_ms"),
        "mdi_median_ms": med(mdi, "render_cpu_ms"),
        "mdi_p95_ms": p95(mdi, "render_cpu_ms"),
        "warm_mdi_over_1_2ms": len(slow),
        "max_slow_streak_1_2ms": longest_slow_streak(measured),
        "shadow_backend_median_ms": med(mdi, "shadow_backend_ms"),
        "opaque_backend_median_ms": med(mdi, "opaque_backend_ms"),
        "shadow_cpu_median_ms": med(mdi, "shadow_cpu_ms"),
        "opaque_cpu_median_ms": med(mdi, "opaque_cpu_ms"),
        "worst_frame": worst["frame"],
        "worst_render_ms": worst["render_cpu_ms"],
        "worst_shadow_backend_ms": worst["shadow_backend_ms"],
        "worst_opaque_backend_ms": worst["opaque_backend_ms"],
        "worst_backend_sum_ms": worst_backend,
        "worst_non_backend_cpu_ms": worst_other_cpu,
        "mdi_backend_median_ms": mdi_cost,
        "slow_mdi_backend_median_ms": slow_mdi_cost,
        "longest_slow_start_frame": longest[0]["frame"] if longest else -1,
        "longest_slow_end_frame": longest[-1]["frame"] if longest else -1,
        "longest_slow_backend_median_ms": longest_cost,
        "longest_slow_non_backend_median_ms": longest_other,
        "longest_slow_mdi_frames": sum(r["mdi_submissions"] > 0 for r in longest),
        "longest_slow_direct_frames": sum(r["mdi_submissions"] == 0 for r in longest),
        "longest_mdi_slow_start_frame": mdi_longest[0]["frame"] if mdi_longest else -1,
        "longest_mdi_slow_end_frame": mdi_longest[-1]["frame"] if mdi_longest else -1,
        "longest_mdi_slow_frames": len(mdi_longest),
        "longest_mdi_slow_backend_median_ms": mdi_longest_cost,
        "longest_mdi_slow_other_cpu_median_ms": mdi_longest_other,
        "longest_direct_slow_start_frame": direct_longest[0]["frame"] if direct_longest else -1,
        "longest_direct_slow_end_frame": direct_longest[-1]["frame"] if direct_longest else -1,
        "longest_direct_slow_frames": len(direct_longest),
        "longest_direct_slow_backend_median_ms": direct_longest_cost,
        "longest_direct_slow_other_cpu_median_ms": direct_longest_other,
    }


def main():
    if len(sys.argv) != 3:
        sys.exit("Usage: batching-stage86-analyze.py OUT_DIR ITEM_COUNT")
    output = pathlib.Path(sys.argv[1])
    items = sys.argv[2]
    results = []
    for label, mode in (
        ("auto1", "auto"), ("mdi1", "mdi"), ("auto2", "auto"),
        ("mdi2", "mdi"), ("auto3", "auto")
    ):
        rows = load_csv(output / f"{label}-frames.csv")
        results.append(stats(label, rows, mode))
    with (output / "summary.csv").open("w", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    print("Stage 86: CPU renderer.render() only, no GPU timestamps/FPS")
    print(f"{'Trial':7} {'MDI/81':>7} {'median':>9} {'p95':>9} "
          f"{'late':>9} {'MDI>1.2':>9} {'streak':>7} {'worst frame':>12}")
    for r in results:
        print(f"{r['trial']:7} {r['mdi_frames']:7d} "
              f"{r['render_median_ms']:9.4f} {r['render_p95_ms']:9.4f} "
              f"{r['late_median_ms']:9.4f} {r['warm_mdi_over_1_2ms']:9d} "
              f"{r['max_slow_streak_1_2ms']:7d} {r['worst_frame']:12d}")
    print("Slow intervals (>1.2ms CPU render; diagnostic threshold, not SLA):")
    for r in results:
        print(f"  {r['trial']}: longest={r['longest_slow_start_frame']}.."
              f"{r['longest_slow_end_frame']}, "
              f"backend_median={r['longest_slow_backend_median_ms']:.4f}ms "
              f"non_backend_cpu_median={r['longest_slow_non_backend_median_ms']:.4f}ms, "
              f"worst_backend={r['worst_backend_sum_ms']:.4f}ms "
              f"worst_non_backend_cpu={r['worst_non_backend_cpu_ms']:.4f}ms")
    print("Mode-separated slow intervals (MDI and Direct never share a streak):")
    for r in results:
        print(f"  {r['trial']}: MDI {r['longest_mdi_slow_start_frame']}.."
              f"{r['longest_mdi_slow_end_frame']} "
              f"({r['longest_mdi_slow_frames']} frames), "
              f"backend={r['longest_mdi_slow_backend_median_ms']:.4f}ms, "
              f"other_cpu={r['longest_mdi_slow_other_cpu_median_ms']:.4f}ms; "
              f"Direct {r['longest_direct_slow_start_frame']}.."
              f"{r['longest_direct_slow_end_frame']} "
              f"({r['longest_direct_slow_frames']} frames), "
              f"other_cpu={r['longest_direct_slow_other_cpu_median_ms']:.4f}ms")
    print(f"Summary: {output / 'summary.csv'}")
    print("NOTE: process-order controls are not randomized per-frame A/B/A;" 
          " interpret results as environment-dependent CPU wall timings.")


if __name__ == "__main__":
    main()
