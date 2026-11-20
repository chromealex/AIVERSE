#!/usr/bin/env python3
"""Audit a Stage129/131/132 Metal capture before it is marked PASS.

This verifies archive-file provenance, expected process inventory, logged pooled
geometry, and raster PPM equality. Stage127/129/132 analyze frame CSV contents.
"""
import csv
import hashlib
from pathlib import Path
import re
import sys
import tempfile

MESHES = (1, 4)
PIPELINES = ("off", "on")
MODES = ("direct", "mdi")
REPEATS = (1, 2)
POOL_LIMIT = 16 * 1024 * 1024
REPORTS = ("stage129-summary.csv", "stage129-scaling.csv",
           "stage132-repetitions.csv", "stage132-reliability.csv")
MESH_REPORTS = ("stage127-runs.csv", "stage127-comparison.csv")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest_ppm(path, dimensions):
    require(path.is_file(), f"missing PPM: {path}")
    with path.open("rb") as handle:
        header = handle.readline() + handle.readline() + handle.readline()
        expected = f"P6\n{dimensions[0]} {dimensions[1]}\n255\n".encode()
        require(header == expected, f"invalid PPM header: {path}")
        h = hashlib.sha256()
        h.update(header)
        pixels = 0
        while block := handle.read(1024 * 256):
            pixels += len(block)
            h.update(block)
        require(pixels == dimensions[0] * dimensions[1] * 3,
                f"invalid PPM RGB length: {path}: {pixels}")
        return h.hexdigest()


def verify(root, dimensions=(960, 640), write=True):
    manifest = root / "manifest.txt"
    require(manifest.is_file(), "missing manifest.txt")
    lines = manifest.read_text().splitlines()
    provenance = dict(line.split("=", 1) for line in lines if "=" in line)
    for key, expected in (("stage", "129"), ("mesh_subdivisions", "1,4"),
                          ("processes", "16"),
                          ("fixed_MDI_topology", "18_submissions_4086_commands"),
                          ("geometry_pool_required", "1")):
        require(provenance.get(key) == expected, f"manifest {key} is not {expected}")
    require(re.fullmatch(r"[0-9a-f]{40}", provenance.get("git_head", "")) is not None,
            "invalid or missing git_head")
    require(provenance.get("result") != "FAILED", "capture explicitly FAILED")
    for report in REPORTS:
        require((root / report).is_file() and (root / report).stat().st_size > 10,
                f"missing completed report: {report}")
    checked = []
    for mesh in MESHES:
        folder = root / f"mesh{mesh}"
        for report in MESH_REPORTS:
            path = folder / report
            require(path.is_file() and path.stat().st_size > 10,
                    f"missing per-mesh report: {path}")
        for pipeline in PIPELINES:
            for mode in MODES:
                for rep in REPEATS:
                    name = f"2048-{pipeline}-{mode}{rep}"
                    stem = folder / name
                    log = stem.with_suffix(".log")
                    require(log.is_file(), f"missing log: {log}")
                    content = log.read_text(errors="replace")
                    require(re.search(r"^material palette CPU checks=\d+ result=passed$",
                                      content, re.MULTILINE), f"CPU checks missing: {log}")
                    pattern = r"^stress diagnostic geometry pooled=(\d+) vertex_bytes=(\d+) index_bytes=(\d+)$"
                    found = re.findall(pattern, content, re.MULTILINE)
                    require(len(found) == 1 and found[0][0] == "1",
                            f"pooled geometry not confirmed: {log}")
                    vb, ib = map(int, found[0][1:])
                    require(vb > 0 and ib > 0 and vb + ib <= POOL_LIMIT,
                            f"invalid pooled upload bytes {vb}+{ib}: {log}")
                    expected = (f"stress diagnostic mesh subdivisions={mesh} "
                                f"triangles_per_primitive={mesh*mesh} "
                                f"vertices_per_primitive={3*mesh*mesh}")
                    require(expected in content, f"unexpected geometry fixture: {log}")
                    require(re.search(r"^GPU command-buffer timeline samples=66 path=", content, re.M),
                            f"missing GPU samples: {log}")
                    require(re.search(r"^stress frame timeline samples=106 path=", content, re.M),
                            f"missing CPU timeline: {log}")
                    require(re.search(r"^changed_channels=0$", content, re.M),
                            f"missing final raster parity: {log}")
                    actual = (f"stress benchmark policy={mode} " + (
                        "measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458"
                        if mode == "mdi" else
                        "measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0"))
                    require(actual in content, f"actual Direct/MDI mode mismatch: {log}")
                    for suffix in ("-gpu-frames.csv", "-frames.csv"):
                        p = Path(str(stem) + suffix)
                        require(p.is_file() and p.stat().st_size > 10,
                                f"missing frame CSV: {p}")
                    hashes = [digest_ppm(Path(str(stem) + suffix), dimensions)
                              for suffix in ("-before.ppm", "-after.ppm")]
                    require(hashes[0] == hashes[1], f"before/after raster mismatch: {stem}")
                    if mode == "mdi":
                        require(re.search(r"^actual MDI pixel parity frame=\d+ changed_channels=0$",
                                          content, re.M), f"MDI parity missing: {log}")
                        extra = digest_ppm(Path(str(stem) + "-mdi-after.ppm"), dimensions)
                        require(extra == hashes[0], f"real MDI raster mismatch: {stem}")
                    checked.append(dict(mesh=mesh, pipeline=pipeline, mode=mode,
                                        repeat=rep, vertex_bytes=vb, index_bytes=ib,
                                        raster_sha256=hashes[0]))
    require(len(checked) == 16, "expected 16 independent processes")
    if write:
        report = root / "stage133-integrity.csv"
        with report.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(checked[0]))
            writer.writeheader()
            writer.writerows(checked)
        print(f"PASS: Stage133 archive integrity; 16 real process files; {report}")
    return checked


def self_test():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "manifest.txt").write_text("stage=129\nmesh_subdivisions=1,4\nprocesses=16\n"
            "fixed_MDI_topology=18_submissions_4086_commands\ngeometry_pool_required=1\n"
            "git_head=" + "a"*40 + "\n")
        for name in REPORTS:
            (root / name).write_text("head,field\nexample,data\n")
        for mesh in MESHES:
            folder = root / f"mesh{mesh}"
            folder.mkdir()
            for name in MESH_REPORTS:
                (folder / name).write_text("head,field\nexample,data\n")
            for pipeline in PIPELINES:
                for mode in MODES:
                    for rep in REPEATS:
                        stem = folder / f"2048-{pipeline}-{mode}{rep}"
                        log = ("material palette CPU checks=38 result=passed\n"
                               f"stress diagnostic mesh subdivisions={mesh} triangles_per_primitive={mesh*mesh} vertices_per_primitive={3*mesh*mesh}\n"
                               "stress diagnostic geometry pooled=1 vertex_bytes=300 index_bytes=200\n"
                               "GPU command-buffer timeline samples=66 path=a\n"
                               "stress frame timeline samples=106 path=a\n"
                               "changed_channels=0\n"
                               f"stress benchmark policy={mode} " + (
                                  "measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=1458\n"
                                  if mode == "mdi" else
                                  "measured_mdi_frames=0 measured_direct_frames=81 measured_mdi_submissions=0\n"))
                        if mode == "mdi":
                            log += "actual MDI pixel parity frame=100 changed_channels=0\n"
                        stem.with_suffix(".log").write_text(log)
                        for suffix in ("-gpu-frames.csv", "-frames.csv"):
                            Path(str(stem) + suffix).write_text("frame,valid\n1,1\n")
                        ppm = b"P6\n1 1\n255\n\x01\x02\x03"
                        for suffix in ("-before.ppm", "-after.ppm") + (("-mdi-after.ppm",) if mode == "mdi" else ()):
                            Path(str(stem) + suffix).write_bytes(ppm)
        assert len(verify(root, dimensions=(1, 1), write=False)) == 16
        corrupt = root / "mesh4" / "2048-on-mdi2-mdi-after.ppm"
        corrupt.write_bytes(b"P6\n1 1\n255\n\x01\x02\x04")
        try:
            verify(root, dimensions=(1, 1), write=False)
        except ValueError as exc:
            assert "raster mismatch" in str(exc), str(exc)
        else:
            raise AssertionError("corrupted MDI raster accepted")
        print("PASS: Stage133 archive audit self-test and raster corruption rejection")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 2:
        verify(Path(sys.argv[1]))
    else:
        sys.exit("Usage: batching-stage133-verify.py CAPTURE_DIR | --self-test")
