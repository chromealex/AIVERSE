#!/usr/bin/env python3
"""Stage137: fixed topology, different projected triangle footprint.

Read Stage127-validated native GPU/CPU run summaries, ensure real pooled
geometry, genuine Direct/MDI and within-process raster parity, and establish
that the screen-space footprint actually changed between scale=1 and scale=4.
GPU timings describe whole completed Metal command buffers, not shader passes.
"""
import csv
import math
from pathlib import Path
import re
import statistics
import sys
import tempfile

SCALES = (1, 4)
PIPELINES = ("off", "on")
MODES = ("direct", "mdi")
REPEATS = (1, 2)
EXPECTED_BYTES = (5898240, 393216)  # 2048 * 16 triangles, 60B vertices / 4B indices


def need(ok, msg):
    if not ok:
        raise ValueError(msg)


def ppm(path, width, height):
    data = path.read_bytes()
    header = f"P6\n{width} {height}\n255\n".encode()
    need(data.startswith(header) and len(data) == len(header) + 3 * width * height,
         f"bad or missing RGB PPM {path}")
    return data[len(header):]


def load(root, scale, width, height):
    folder = root / f"scale{scale}"
    path = folder / "stage127-runs.csv"
    with path.open(newline="") as stream:
        records = list(csv.DictReader(stream))
    need(len(records) == 8, f"{path}: expected 8 actual runs")
    result = {}
    for row in records:
        pipeline, mode, rep = row["quality"], row["mode"], int(row["repeat"])
        key = (pipeline, mode, rep)
        need(pipeline in PIPELINES and mode in MODES and rep in REPEATS and key not in result,
             f"{path}: duplicate/invalid key {key}")
        need(int(row["gpu_samples"]) == 66, f"{key}: GPU sample count is not 66")
        topology = tuple(int(row[x]) for x in
                         ("mdi_submissions", "mdi_commands", "shadow_draws", "opaque_draws"))
        expected = (0, 0, 2048, 2048) if mode == "direct" else (18, 4086, 14, 14)
        need(topology == expected, f"{key}: incorrect actual GPU command topology {topology}")
        values = {}
        for field in ("gpu_median_ms", "gpu_p95_ms", "cpu_median_ms"):
            values[field] = float(row[field])
            need(math.isfinite(values[field]) and values[field] > 0,
                 f"{key}: nonfinite or nonpositive {field}")
        need(values["gpu_p95_ms"] >= values["gpu_median_ms"],
             f"{key}: P95 below median")
        stem = folder / f"2048-{pipeline}-{mode}{rep}"
        log = stem.with_suffix(".log").read_text(errors="replace")
        need(f"stress diagnostic triangle_scale={scale}\n" in log,
             f"{stem}: wrong or missing triangle scale")
        need("stress diagnostic mesh subdivisions=4 triangles_per_primitive=16 vertices_per_primitive=48\n" in log,
             f"{stem}: geometry topology changed")
        m = re.findall(r"^stress diagnostic geometry pooled=(\d+) vertex_bytes=(\d+) index_bytes=(\d+)$",
                       log, re.MULTILINE)
        need(len(m) == 1 and m[0][0] == "1" and
             tuple(map(int, m[0][1:])) == EXPECTED_BYTES,
             f"{stem}: Geometry Pool allocation changed")
        need(re.search(r"^material palette CPU checks=38 result=passed$", log, re.M),
             f"{stem}: missing CPU invariants")
        need(re.search(r"^changed_channels=0$", log, re.M),
             f"{stem}: final parity missing")
        before = ppm(Path(str(stem) + "-before.ppm"), width, height)
        after = ppm(Path(str(stem) + "-after.ppm"), width, height)
        need(before == after, f"{stem}: within-run PPM mismatch")
        if mode == "mdi":
            need(re.search(r"^actual MDI pixel parity frame=\d+ changed_channels=0$", log, re.M),
                 f"{stem}: MDI frame parity not recorded")
            mdi = ppm(Path(str(stem) + "-mdi-after.ppm"), width, height)
            need(mdi == before, f"{stem}: real MDI PPM mismatch")
        result[key] = dict(**values, before=before)
    need(len(result) == 8, f"{path}: incomplete run matrix")
    with (folder / "stage127-comparison.csv").open(newline="") as stream:
        summary = list(csv.DictReader(stream))
    need(len(summary) == 2 and {r["quality"] for r in summary} == set(PIPELINES),
         f"{folder}: missing Stage127 pipeline summaries")
    for row in summary:
        pipeline = row["quality"]
        direct = statistics.median(result[(pipeline, "direct", rep)]["gpu_median_ms"]
                                   for rep in REPEATS)
        mdi = statistics.median(result[(pipeline, "mdi", rep)]["gpu_median_ms"]
                                for rep in REPEATS)
        for column, calculated in (("gpu_direct_ms", direct),
                                   ("gpu_mdi_ms", mdi),
                                   ("gpu_mdi_premium_ms", mdi - direct)):
            need(abs(float(row[column]) - calculated) <= 1e-6,
                 f"{folder}: {pipeline} stale {column}")
    return result


def analyze(root, width=960, height=640, min_changed_pixels=32, write=True):
    manifest = dict(line.split("=", 1) for line in (root / "manifest.txt").read_text().splitlines()
                    if "=" in line)
    for key, expected in (("stage", "137"), ("mesh_subdivisions", "4"),
                          ("triangle_scales", "1,4"), ("processes", "16"),
                          ("fixed_MDI_topology", "18_submissions_4086_commands")):
        need(manifest.get(key) == expected, f"manifest {key} != {expected}")
    if manifest.get("native_provenance"):
        need(manifest["native_provenance"] == "native-provenance.txt", "unrecognized provenance")
        native = dict(line.split("=", 1) for line in
                      (root / "native-provenance.txt").read_text().splitlines() if "=" in line)
        need(native.get("stage") == "137" and
             native.get("repo_head") == manifest.get("git_head") and
             re.fullmatch(r"[0-9a-f]{64}", native.get("binary_sha256", "")),
             "native provenance/compiled executable mismatched")
    runs = {scale: load(root, scale, width, height) for scale in SCALES}
    small = runs[1][("off", "direct", 1)]["before"]
    large = runs[4][("off", "direct", 1)]["before"]
    changed_pixels = sum(small[i:i+3] != large[i:i+3] for i in range(0, len(small), 3))
    need(changed_pixels >= min_changed_pixels,
         f"triangle scale produced only {changed_pixels} different pixels; raster control invalid")
    rows = []
    for pipeline in PIPELINES:
        a = runs[1]
        b = runs[4]
        for rep in REPEATS:
            small_direct = a[(pipeline, "direct", rep)]["gpu_median_ms"]
            small_mdi = a[(pipeline, "mdi", rep)]["gpu_median_ms"]
            big_direct = b[(pipeline, "direct", rep)]["gpu_median_ms"]
            big_mdi = b[(pipeline, "mdi", rep)]["gpu_median_ms"]
            rows.append(dict(
                pipeline=pipeline, repeat=rep, changed_raster_pixels=changed_pixels,
                small_direct_gpu_ms=small_direct, small_mdi_gpu_ms=small_mdi,
                large_direct_gpu_ms=big_direct, large_mdi_gpu_ms=big_mdi,
                small_mdi_premium_ms=small_mdi-small_direct,
                large_mdi_premium_ms=big_mdi-big_direct,
                premium_coverage_delta_ms=(big_mdi-big_direct)-(small_mdi-small_direct),
                small_direct_p95_ms=a[(pipeline, "direct", rep)]["gpu_p95_ms"],
                small_mdi_p95_ms=a[(pipeline, "mdi", rep)]["gpu_p95_ms"],
                large_direct_p95_ms=b[(pipeline, "direct", rep)]["gpu_p95_ms"],
                large_mdi_p95_ms=b[(pipeline, "mdi", rep)]["gpu_p95_ms"],
                small_direct_cpu_ms=a[(pipeline, "direct", rep)]["cpu_median_ms"],
                small_mdi_cpu_ms=a[(pipeline, "mdi", rep)]["cpu_median_ms"],
                large_direct_cpu_ms=b[(pipeline, "direct", rep)]["cpu_median_ms"],
                large_mdi_cpu_ms=b[(pipeline, "mdi", rep)]["cpu_median_ms"]))
    if write:
        path = root / "stage137-coverage-comparison.csv"
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"Stage137 PASS: 16 pooled processes; {changed_pixels} raster pixels changed")
        for pipeline in PIPELINES:
            effect = [x["premium_coverage_delta_ms"] for x in rows if x["pipeline"] == pipeline]
            print(f"Stage137 pipeline={pipeline} MDI premium coverage effect repeats: "
                  f"{effect[0]:+.6f}ms, {effect[1]:+.6f}ms")
        print(f"Stage137 report: {path}")
        print("Whole completed Metal command buffers only. 2 runs/cell are not statistical proof.")
    return rows


def fixture(root):
    (root / "manifest.txt").write_text(
        "stage=137\nmesh_subdivisions=4\ntriangle_scales=1,4\nprocesses=16\n"
        "fixed_MDI_topology=18_submissions_4086_commands\n")
    for scale in SCALES:
        folder = root / f"scale{scale}"
        folder.mkdir()
        records = []
        for pipeline in PIPELINES:
            for mode in MODES:
                for rep in REPEATS:
                    gpu = (0.6 + (0.05 if scale == 4 else 0) +
                           (0.2 if mode == "mdi" else 0) +
                           (0.02 if pipeline == "on" else 0) + rep*0.001)
                    topology = (0, 0, 2048, 2048) if mode == "direct" else (18, 4086, 14, 14)
                    records.append(dict(quality=pipeline,mode=mode,repeat=rep,gpu_samples=66,
                                        gpu_median_ms=gpu,gpu_p95_ms=gpu+.05,
                                        cpu_median_ms=1.3 if mode=="mdi" else 2.4,
                                        mdi_submissions=topology[0],mdi_commands=topology[1],
                                        shadow_draws=topology[2],opaque_draws=topology[3]))
                    stem=folder / f"2048-{pipeline}-{mode}{rep}"
                    text = ("material palette CPU checks=38 result=passed\n"
                            "stress diagnostic mesh subdivisions=4 triangles_per_primitive=16 vertices_per_primitive=48\n"
                            f"stress diagnostic triangle_scale={scale}\n"
                            "stress diagnostic geometry pooled=1 vertex_bytes=5898240 index_bytes=393216\n"
                            "changed_channels=0\n")
                    if mode=="mdi":
                        text += "actual MDI pixel parity frame=100 changed_channels=0\n"
                    stem.with_suffix(".log").write_text(text)
                    pixels = bytes([scale, 1, 1]*16)
                    header = b"P6\n4 4\n255\n"
                    for suffix in ("-before.ppm","-after.ppm") + (("-mdi-after.ppm",) if mode=="mdi" else ()):
                        Path(str(stem)+suffix).write_bytes(header+pixels)
        with (folder / "stage127-runs.csv").open("w", newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(records[0]))
            writer.writeheader(); writer.writerows(records)
        sums=[]
        for pipeline in PIPELINES:
            a=statistics.median(x["gpu_median_ms"] for x in records if x["quality"]==pipeline and x["mode"]=="direct")
            b=statistics.median(x["gpu_median_ms"] for x in records if x["quality"]==pipeline and x["mode"]=="mdi")
            sums.append(dict(quality=pipeline,gpu_direct_ms=a,gpu_mdi_ms=b,gpu_mdi_premium_ms=b-a))
        with (folder / "stage127-comparison.csv").open("w",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(sums[0]));writer.writeheader();writer.writerows(sums)


def self_test():
    with tempfile.TemporaryDirectory() as td:
        root=Path(td)
        fixture(root)
        rows=analyze(root,width=4,height=4,min_changed_pixels=1,write=False)
        need(len(rows)==4, "synthetic run inventory not 4 pairs")
        need(all(abs(r["premium_coverage_delta_ms"])<1e-9 for r in rows),
             "synthetic constant GPU premium wrong")
        corrupt=root / "scale4" / "2048-on-mdi2-mdi-after.ppm"
        corrupt.write_bytes(b"P6\n4 4\n255\n"+bytes([99,1,1]*16))
        try:
            analyze(root,width=4,height=4,min_changed_pixels=1,write=False)
        except ValueError as ex:
            need("MDI PPM mismatch" in str(ex), str(ex))
        else:
            raise AssertionError("corrupted real MDI frame was accepted")
        print("PASS: Stage137 fixed-topology raster scaling synthetic controls + MDI parity rejection")


if __name__ == "__main__":
    if len(sys.argv)==2 and sys.argv[1]=="--self-test":
        self_test()
    elif len(sys.argv)==2:
        analyze(Path(sys.argv[1]))
    else:
        sys.exit("Usage: batching-stage137-analyze.py CAPTURE_DIR | --self-test")
