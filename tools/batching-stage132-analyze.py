#!/usr/bin/env python3
"""Stage132: paired per-process reliability diagnostics for Stage129/131 Metal trials.

Whole completed GPU command-buffer timings only; this is not a statistical
significance test, model-pass GPU clock, or decision to enable production MDI.
Requires Stage127 per-run CSVs from both pooled mesh1 and mesh4 captures.
"""
import csv
import math
from pathlib import Path
import statistics
import sys
import tempfile

MESHES = (1, 4)
PIPELINES = ("off", "on")
MODES = ("direct", "mdi")
REPS = (1, 2)
RUN_FIELDS = ("quality", "mode", "repeat", "gpu_samples",
              "gpu_median_ms", "gpu_p95_ms", "gpu_max_ms",
              "cpu_median_ms", "cpu_p95_ms",
              "mdi_submissions", "mdi_commands", "shadow_draws", "opaque_draws")


def read_csv(path):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not set(RUN_FIELDS).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: missing Stage127 run columns")
        return list(reader)


def validated_runs(root, mesh):
    path = root / f"mesh{mesh}" / "stage127-runs.csv"
    rows = read_csv(path)
    if len(rows) != 8:
        raise ValueError(f"{path}: expected exactly 8 actual process records")
    result = {}
    for row in rows:
        pipeline, mode = row["quality"], row["mode"]
        rep = int(row["repeat"])
        key = pipeline, mode, rep
        if pipeline not in PIPELINES or mode not in MODES or rep not in REPS or key in result:
            raise ValueError(f"{path}: duplicate or invalid process key {key}")
        if int(row["gpu_samples"]) != 66:
            raise ValueError(f"{path}: {key} requires exactly 66 GPU timestamps")
        topology = tuple(int(row[field]) for field in (
            "mdi_submissions", "mdi_commands", "shadow_draws", "opaque_draws"))
        expected = (0, 0, 2048, 2048) if mode == "direct" else (18, 4086, 14, 14)
        if topology != expected:
            raise ValueError(f"{path}: {key} actual draw topology {topology} != {expected}")
        numeric = {}
        for field in ("gpu_median_ms", "gpu_p95_ms", "gpu_max_ms",
                      "cpu_median_ms", "cpu_p95_ms"):
            value = float(row[field])
            if not math.isfinite(value) or value <= 0:
                raise ValueError(f"{path}: {key} invalid {field}={value}")
            numeric[field] = value
        if not (numeric["gpu_median_ms"] <= numeric["gpu_p95_ms"] <= numeric["gpu_max_ms"]
                and numeric["cpu_median_ms"] <= numeric["cpu_p95_ms"]):
            raise ValueError(f"{path}: {key} invalid percentile ordering")
        result[key] = numeric
    if len(result) != 8:
        raise ValueError(f"{path}: missing process controls")
    return result


def require_summary_consistency(root, mesh, runs):
    path = root / f"mesh{mesh}" / "stage127-comparison.csv"
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 2 or {r["quality"] for r in rows} != set(PIPELINES):
        raise ValueError(f"{path}: incomplete OFF/ON Stage127 summary")
    for row in rows:
        pipeline = row["quality"]
        gd = statistics.median(runs[pipeline, "direct", rep]["gpu_median_ms"] for rep in REPS)
        gm = statistics.median(runs[pipeline, "mdi", rep]["gpu_median_ms"] for rep in REPS)
        cd = statistics.median(runs[pipeline, "direct", rep]["cpu_median_ms"] for rep in REPS)
        cm = statistics.median(runs[pipeline, "mdi", rep]["cpu_median_ms"] for rep in REPS)
        fields = {"gpu_direct_ms": gd, "gpu_mdi_ms": gm,
                  "gpu_mdi_premium_ms": gm - gd,
                  "cpu_direct_ms": cd, "cpu_mdi_ms": cm}
        for field, expected in fields.items():
            measured = float(row[field])
            if not math.isfinite(measured) or abs(measured - expected) > 1e-6:
                raise ValueError(f"{path}: stale or mismatched {pipeline} {field}")
        if (int(row["items"]), int(row["mdi_submissions"]),
                int(row["mdi_commands"])) != (2048, 18, 4086):
            raise ValueError(f"{path}: invalid summary topology")


def analyze(root, write=True):
    runsets = {mesh: validated_runs(root, mesh) for mesh in MESHES}
    for mesh in MESHES:
        require_summary_consistency(root, mesh, runsets[mesh])
    repetitions = []
    conclusions = []
    for pipeline in PIPELINES:
        spans = {}
        for mesh in MESHES:
            for mode in MODES:
                first = runsets[mesh][pipeline, mode, 1]["gpu_median_ms"]
                second = runsets[mesh][pipeline, mode, 2]["gpu_median_ms"]
                spans[(mesh, mode)] = abs(second - first)
        # This is a *descriptive* bound on process-to-process drift, not
        # an uncertainty interval or an independent estimate of noise.
        drift_budget = max(spans.values())
        effects = []
        for rep in REPS:
            r1 = runsets[1]
            r4 = runsets[4]
            low_direct = r1[pipeline, "direct", rep]["gpu_median_ms"]
            low_mdi = r1[pipeline, "mdi", rep]["gpu_median_ms"]
            high_direct = r4[pipeline, "direct", rep]["gpu_median_ms"]
            high_mdi = r4[pipeline, "mdi", rep]["gpu_median_ms"]
            low_premium = low_mdi - low_direct
            high_premium = high_mdi - high_direct
            effect = high_premium - low_premium
            effects.append(effect)
            repetitions.append(dict(
                pipeline=pipeline, repeat=rep,
                mesh1_gpu_direct_ms=low_direct, mesh1_gpu_mdi_ms=low_mdi,
                mesh4_gpu_direct_ms=high_direct, mesh4_gpu_mdi_ms=high_mdi,
                mesh1_mdi_premium_ms=low_premium, mesh4_mdi_premium_ms=high_premium,
                premium_scaling_ms=effect,
                mesh1_direct_p95_ms=r1[pipeline, "direct", rep]["gpu_p95_ms"],
                mesh1_mdi_p95_ms=r1[pipeline, "mdi", rep]["gpu_p95_ms"],
                mesh4_direct_p95_ms=r4[pipeline, "direct", rep]["gpu_p95_ms"],
                mesh4_mdi_p95_ms=r4[pipeline, "mdi", rep]["gpu_p95_ms"],
                mesh1_cpu_direct_median_ms=r1[pipeline, "direct", rep]["cpu_median_ms"],
                mesh1_cpu_mdi_median_ms=r1[pipeline, "mdi", rep]["cpu_median_ms"],
                mesh4_cpu_direct_median_ms=r4[pipeline, "direct", rep]["cpu_median_ms"],
                mesh4_cpu_mdi_median_ms=r4[pipeline, "mdi", rep]["cpu_median_ms"]))
        same_direction = effects[0] * effects[1] > 0
        exceeds = all(abs(effect) > drift_budget for effect in effects)
        verdict = ("directional_candidate" if same_direction and exceeds
                   else "inconclusive")
        conclusions.append(dict(
            pipeline=pipeline, paired_effect_repeat1_ms=effects[0],
            paired_effect_repeat2_ms=effects[1],
            median_paired_effect_ms=statistics.median(effects),
            process_drift_budget_ms=drift_budget,
            mesh1_direct_span_ms=spans[1, "direct"],
            mesh1_mdi_span_ms=spans[1, "mdi"],
            mesh4_direct_span_ms=spans[4, "direct"],
            mesh4_mdi_span_ms=spans[4, "mdi"],
            qualitative_signal=verdict))
    if write:
        for name, records in (("stage132-repetitions.csv", repetitions),
                              ("stage132-reliability.csv", conclusions)):
            with (root / name).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(records[0]))
                writer.writeheader()
                writer.writerows(records)
            print(f"Report: {root / name}")
        for row in conclusions:
            print(f"Stage132 {row['pipeline']}: paired N4-N1 MDI premium scaling "
                  f"{row['paired_effect_repeat1_ms']:+.6f}ms / "
                  f"{row['paired_effect_repeat2_ms']:+.6f}ms, "
                  f"max process span={row['process_drift_budget_ms']:.6f}ms "
                  f"=> {row['qualitative_signal']}")
        print("Two repeats are NOT statistical significance; candidate is only"
              " a repeatable direction exceeding the observed process span.")
        print("GPU numbers represent entire completed Metal command buffers,"
              " not GPU model pass / isolated shader cost. Never automatically"
              " enable MDI based on this diagnostic.")
    return repetitions, conclusions


def fixture(root, scale_rep2=0.0):
    for mesh in MESHES:
        folder = root / f"mesh{mesh}"
        folder.mkdir()
        rows = []
        for pipeline in PIPELINES:
            for mode in MODES:
                for rep in REPS:
                    # Strong geometry-dependent signal in both inherited modes.
                    gpu = (0.50 + (0.03 if mesh == 4 else 0.0)
                           + (0.20 if mode == "mdi" else 0.0)
                           + (0.08 if mesh == 4 and mode == "mdi" else 0.0)
                           + (0.01 if pipeline == "on" else 0.0)
                           + (scale_rep2 if rep == 2 else 0.0))
                    cpu = 1.1 if mode == "mdi" else 2.1
                    rows.append(dict(quality=pipeline, mode=mode, repeat=rep,
                                     gpu_samples=66, gpu_median_ms=gpu,
                                     gpu_p95_ms=gpu+0.02, gpu_max_ms=gpu+0.04,
                                     cpu_median_ms=cpu, cpu_p95_ms=cpu+0.1,
                                     mdi_submissions=18 if mode=="mdi" else 0,
                                     mdi_commands=4086 if mode=="mdi" else 0,
                                     shadow_draws=14 if mode=="mdi" else 2048,
                                     opaque_draws=14 if mode=="mdi" else 2048))
        with (folder / "stage127-runs.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=RUN_FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        sums = []
        for pipeline in PIPELINES:
            rd = [r for r in rows if r["quality"] == pipeline and r["mode"]=="direct"]
            rm = [r for r in rows if r["quality"] == pipeline and r["mode"]=="mdi"]
            gd = statistics.median(r["gpu_median_ms"] for r in rd)
            gm = statistics.median(r["gpu_median_ms"] for r in rm)
            cd = statistics.median(r["cpu_median_ms"] for r in rd)
            cm = statistics.median(r["cpu_median_ms"] for r in rm)
            sums.append(dict(quality=pipeline, items=2048, mdi_submissions=18,
                             mdi_commands=4086, gpu_direct_ms=gd, gpu_mdi_ms=gm,
                             gpu_mdi_premium_ms=gm-gd, cpu_direct_ms=cd,
                             cpu_mdi_ms=cm))
        with (folder / "stage127-comparison.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(sums[0]))
            writer.writeheader()
            writer.writerows(sums)


def self_test():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        fixture(root)
        repetitions, results = analyze(root, write=False)
        assert len(repetitions) == 4 and len(results) == 2
        assert all(row["qualitative_signal"] == "directional_candidate" for row in results)
        assert all(abs(row["median_paired_effect_ms"] - 0.08) < 1e-9
                   for row in results)
        # Failure must be loud, not silently discard inconsistent experiment.
        path = root / "mesh4" / "stage127-runs.csv"
        data = path.read_text()
        path.write_text(data.replace(",4086,14,14", ",4085,14,14", 1))
        try:
            analyze(root, write=False)
        except ValueError as error:
            assert "topology" in str(error)
        else:
            raise AssertionError("corrupted MDI commands accepted")
        # Restore and model a drift large enough to make effect inconclusive.
        path.unlink()
        (root / "mesh4" / "stage127-comparison.csv").unlink()
        (root / "mesh1" / "stage127-runs.csv").unlink()
        (root / "mesh1" / "stage127-comparison.csv").unlink()
        (root / "mesh4").rmdir()
        (root / "mesh1").rmdir()
        fixture(root, scale_rep2=0.25)
        _, noisy = analyze(root, write=False)
        assert all(row["qualitative_signal"] == "inconclusive" for row in noisy)
        print("PASS: Stage132 synthetic paired controls, corruption rejection,"
              " and drift-inconclusive checks")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 2:
        analyze(Path(sys.argv[1]))
    else:
        sys.exit("Usage: batching-stage132-analyze.py CAPTURE_DIR | --self-test")
