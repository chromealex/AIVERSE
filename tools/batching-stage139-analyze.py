#!/usr/bin/env python3
"""Stage139: fixed N4 raster, Metal fragmentless-depth ICB uniform binding A/B.

Requires Stage127's strict frame/topology parser, exact native process parity,
and proof that the optional path executed for real MDI submissions.
Whole Metal command-buffer GPU timings are not per-pass or shader timers.
"""
import csv
import hashlib
import math
from pathlib import Path
import re
import runpy
import statistics
import sys
import tempfile

ORDER = (
    ("off", "direct", 1), ("off", "mdi", 1),
    ("on", "direct", 1), ("on", "mdi", 1),
    ("on", "mdi", 2), ("on", "direct", 2),
    ("off", "mdi", 2), ("off", "direct", 2),
    ("off", "direct", 3), ("off", "mdi", 3),
    ("on", "direct", 3), ("on", "mdi", 3),
)
MODES = ("off", "on")
REPEATS = (1, 2, 3)
POOL_BYTES = (5898240, 393216)
GPU_EXPECTED = 66
CPU_EXPECTED = 81


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_ppm(path, size):
    require(path.is_file(), f"missing PPM {path}")
    with path.open("rb") as handle:
        header = handle.readline() + handle.readline() + handle.readline()
        expect = f"P6\n{size[0]} {size[1]}\n255\n".encode()
        require(header == expect, f"bad PPM header {path}")
        pixel_bytes = handle.read()
    require(len(pixel_bytes) == size[0] * size[1] * 3,
            f"wrong RGB length: {path}")
    return hashlib.sha256(pixel_bytes).hexdigest()


def check_native(root, mode, draw, repeat, size):
    name = f"2048-{mode}-{draw}{repeat}"
    stem = root / name
    log = stem.with_suffix(".log")
    require(log.is_file(), f"missing process log: {log}")
    text = log.read_text(errors="replace")
    checks = (
        "stress diagnostic mesh subdivisions=4 triangles_per_primitive=16 vertices_per_primitive=48",
        "stress diagnostic triangle_scale=4",
        "stress diagnostic geometry pooled=1 vertex_bytes=5898240 index_bytes=393216",
        "stress diagnostic CPU MDI group cap=0",
        "stress diagnostic model pass=full mask=3",
    )
    for pattern in checks:
        require(pattern in text, f"{name}: missing {pattern}")
    require(re.search(r"^material palette CPU checks=38 result=passed$", text, re.M),
            f"{name}: missing CPU check success")
    require(re.search(r"^changed_channels=0$", text, re.M),
            f"{name}: missing raster parity")
    require(re.search(r"^GPU command-buffer timeline samples=66 path=", text, re.M),
            f"{name}: missing completed GPU timeline")
    require(re.search(r"^stress frame timeline samples=106 path=", text, re.M),
            f"{name}: missing CPU timeline")
    expected_mode = (f"stress benchmark policy={draw} " + (
        "measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458"
        if draw == "mdi" else
        "measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0"))
    require(expected_mode in text, f"{name}: wrong actual Direct/MDI mode")
    marker = re.findall(r"^\[metal-icb-depth-uniforms\] mode=(\w+) eligible=(\d+) slots=(\d+)$",
                        text, re.M)
    if draw == "mdi":
        require(len(marker) == 1 and marker[0][0] == mode and
                marker[0][1] == "1" and int(marker[0][2]) > 0,
                f"{name}: experiment did not reach a fragmentless depth pipeline")
        require(re.search(r"^actual MDI pixel parity frame=\d+ changed_channels=0$",
                          text, re.M), f"{name}: missing real MDI parity")
    else:
        require(not marker, f"{name}: Direct process unexpectedly executed ICB")
    before = read_ppm(Path(str(stem) + "-before.ppm"), size)
    after = read_ppm(Path(str(stem) + "-after.ppm"), size)
    require(before == after, f"{name}: before/after pixel mismatch")
    if draw == "mdi":
        real = read_ppm(Path(str(stem) + "-mdi-after.ppm"), size)
        require(real == before, f"{name}: actual MDI pixel mismatch")
    return marker[0][2] if marker else "0"


def analyze(root, size=(960, 640), write=True):
    manifest_path = root / "manifest.txt"
    require(manifest_path.is_file(), "missing manifest")
    fields = dict(line.split("=", 1) for line in manifest_path.read_text().splitlines()
                  if "=" in line)
    for key, expected in (
        ("stage", "139"), ("items", "2048"), ("processes", "12"),
        ("mesh_subdivisions", "4"), ("triangle_scale", "4"),
        ("icb_inherit_pipeline", "0"),
        ("depth_fragment_uniform_skip_modes", "off,on"),
        ("geometry_pool_vertex_bytes", "5898240"),
        ("geometry_pool_index_bytes", "393216"),
        ("geometry_pool_required", "1"),
    ):
        require(fields.get(key) == expected,
                f"manifest {key} mismatch: {fields.get(key)} != {expected}")
    require(fields.get("result") != "FAILED", "capture reports FAILED")
    if "native_provenance" in fields:
        require(fields["native_provenance"] == "native-provenance.txt",
                "unexpected native provenance path")
        native = dict(line.split("=", 1) for line in
                      (root / "native-provenance.txt").read_text().splitlines() if "=" in line)
        require(native.get("repo_head") == fields.get("git_head") and
                native.get("stage") == "139" and
                re.fullmatch(r"[0-9a-f]{64}", native.get("binary_sha256", "")),
                "native source/binary provenance mismatch")
    # Import the existing Stage127 strict full GPU/CPU per-frame topology parser.
    parser_file = Path(__file__).with_name("batching-stage127-analyze.py")
    native_run = runpy.run_path(str(parser_file))["run"]
    rows = []
    results = {}
    for mode, draw, rep in ORDER:
        bindings = check_native(root, mode, draw, rep, size)
        record = native_run(root, mode, draw, rep)
        require(record["gpu_samples"] == GPU_EXPECTED,
                "missing completed GPU samples")
        record["fragment_uniform_bindings"] = bindings
        rows.append(record)
        results[(mode, draw, rep)] = record
    require(len(results) == 12, "process inventory mismatch")
    pairs = []
    for rep in REPEATS:
        offd, offm = results[("off", "direct", rep)], results[("off", "mdi", rep)]
        ond, onm = results[("on", "direct", rep)], results[("on", "mdi", rep)]
        premium_off = offm["gpu_median_ms"] - offd["gpu_median_ms"]
        premium_on = onm["gpu_median_ms"] - ond["gpu_median_ms"]
        pairs.append(dict(
            repeat=rep,
            off_direct_gpu_ms=offd["gpu_median_ms"],
            off_mdi_gpu_ms=offm["gpu_median_ms"],
            on_direct_gpu_ms=ond["gpu_median_ms"],
            on_mdi_gpu_ms=onm["gpu_median_ms"],
            off_mdi_gpu_premium_ms=premium_off,
            on_mdi_gpu_premium_ms=premium_on,
            premium_improvement_ms=premium_off-premium_on,
            off_direct_gpu_p95_ms=offd["gpu_p95_ms"],
            off_mdi_gpu_p95_ms=offm["gpu_p95_ms"],
            on_direct_gpu_p95_ms=ond["gpu_p95_ms"],
            on_mdi_gpu_p95_ms=onm["gpu_p95_ms"],
            off_direct_cpu_ms=offd["cpu_median_ms"],
            off_mdi_cpu_ms=offm["cpu_median_ms"],
            on_direct_cpu_ms=ond["cpu_median_ms"],
            on_mdi_cpu_ms=onm["cpu_median_ms"],
            depth_only_uniform_slots=int(onm["fragment_uniform_bindings"])))
    summary = []
    for mode in MODES:
        d = [results[(mode, "direct", rep)] for rep in REPEATS]
        m = [results[(mode, "mdi", rep)] for rep in REPEATS]
        dg = statistics.median(x["gpu_median_ms"] for x in d)
        mg = statistics.median(x["gpu_median_ms"] for x in m)
        dc = statistics.median(x["cpu_median_ms"] for x in d)
        mc = statistics.median(x["cpu_median_ms"] for x in m)
        summary.append(dict(
            mode=mode, processes=6,
            gpu_direct_median_ms=dg, gpu_mdi_median_ms=mg,
            gpu_mdi_premium_ms=mg-dg,
            gpu_mdi_change_pct=100*(mg/dg-1),
            cpu_direct_median_ms=dc, cpu_mdi_median_ms=mc,
            cpu_mdi_change_pct=100*(mc/dc-1)))
    effects = [row["premium_improvement_ms"] for row in pairs]
    if write:
        for name, records in (("stage139-runs.csv", rows),
                              ("stage139-paired.csv", pairs),
                              ("stage139-summary.csv", summary)):
            path = root / name
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(records[0]))
                writer.writeheader()
                writer.writerows(records)
            print(f"Report: {path}")
        print("Depth-only fragment uniform skip: ON reduces GPU premium "
              "only if independent matched comparisons are repeatable.")
        print("GPU premium improvements by repeat (positive=benefit): " +
              ", ".join(f"{x:+.6f}ms" for x in effects))
        print("Whole completed Metal command buffers, not model-only GPU time.")
        print("Three process repeats per cell are exploratory, not statistical proof.")
    return rows, pairs, summary


def self_test():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "manifest.txt").write_text(
            "stage=139\nitems=2048\nprocesses=12\nmesh_subdivisions=4\n"
            "triangle_scale=4\nicb_inherit_pipeline=0\n"
            "depth_fragment_uniform_skip_modes=off,on\n"
            "geometry_pool_vertex_bytes=5898240\n"
            "geometry_pool_index_bytes=393216\ngeometry_pool_required=1\n")
        for mode, draw, rep in ORDER:
            stem = root / f"2048-{mode}-{draw}{rep}"
            log = ("material palette CPU checks=38 result=passed\n"
                   "stress diagnostic mesh subdivisions=4 triangles_per_primitive=16 vertices_per_primitive=48\n"
                   "stress diagnostic triangle_scale=4\n"
                   "stress diagnostic geometry pooled=1 vertex_bytes=5898240 index_bytes=393216\n"
                   "stress diagnostic CPU MDI group cap=0\n"
                   "stress diagnostic model pass=full mask=3\n"
                   "changed_channels=0\n"
                   "GPU command-buffer timeline samples=66 path=x\n"
                   "stress frame timeline samples=106 path=x\n"
                   f"stress benchmark policy={draw} " + (
                       "measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458\n"
                       if draw == "mdi" else
                       "measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0\n"))
            if draw == "mdi":
                log += (f"[metal-icb-depth-uniforms] mode={mode} eligible=1 slots=3\n"
                        "actual MDI pixel parity frame=100 changed_channels=0\n")
            stem.with_suffix(".log").write_text(log)
            ppm = b"P6\n1 1\n255\n\x02\x03\x04"
            for suffix in ("-before.ppm", "-after.ppm") + (
                    ("-mdi-after.ppm",) if draw == "mdi" else ()):
                Path(str(stem)+suffix).write_bytes(ppm)
            with Path(str(stem)+"-gpu-frames.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=(
                    "frame", "gpu_command_buffer_ms", "valid"))
                writer.writeheader()
                for frame in range(115, 181):
                    gpu = 0.5 + (0.2 if draw=="mdi" else 0) + (
                        -0.03 if draw=="mdi" and mode=="on" else 0) + rep*.001
                    writer.writerow(dict(frame=frame,gpu_command_buffer_ms=gpu,valid=1))
            with Path(str(stem)+"-frames.csv").open("w",newline="") as stream:
                writer=csv.DictWriter(stream,fieldnames=(
                    "frame","render_cpu_ms","mdi_submissions","mdi_commands",
                    "shadow_draws","opaque_draws"))
                writer.writeheader()
                for frame in range(90,196):
                    indirect=draw=="mdi"
                    writer.writerow(dict(frame=frame,render_cpu_ms=1.2 if indirect else 2.4,
                                         mdi_submissions=18 if indirect else 0,
                                         mdi_commands=4086 if indirect else 0,
                                         shadow_draws=14 if indirect else 2048,
                                         opaque_draws=14 if indirect else 2048))
        rows,pairs,summary=analyze(root,size=(1,1),write=False)
        assert len(rows)==12 and len(pairs)==3 and len(summary)==2
        assert all(abs(p["premium_improvement_ms"]-.03)<1e-8 for p in pairs)
        corrupt=root/"2048-on-mdi3-mdi-after.ppm"
        corrupt.write_bytes(b"P6\n1 1\n255\n\x02\x03\x05")
        try:
            analyze(root,size=(1,1),write=False)
        except ValueError as exc:
            assert "actual MDI pixel mismatch" in str(exc),str(exc)
        else:
            raise AssertionError("corrupted real-MDI raster accepted")
        print("PASS: Stage139 synthetic CPU/GPU timeline, 12-case A/B and MDI parity self-test")


if __name__=="__main__":
    if len(sys.argv)==2 and sys.argv[1]=="--self-test":
        self_test()
    elif len(sys.argv)==2:
        analyze(Path(sys.argv[1]))
    else:
        sys.exit("Usage: batching-stage139-analyze.py CAPTURE_DIR | --self-test")
