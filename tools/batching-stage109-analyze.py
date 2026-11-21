#!/usr/bin/env python3
"""Stage 109 repeated cross-order GPU command-buffer timing validation."""
import csv
import math
import pathlib
import statistics
import sys

def p95(values):
    return sorted(values)[math.ceil(.95 * len(values))-1]

def read_gpu(path):
    with path.open(newline="") as fp:
        reader=csv.DictReader(fp)
        if reader.fieldnames != ["frame","gpu_command_buffer_ms","valid"]:
            raise ValueError(f"{path}: wrong GPU columns")
        rows=list(reader)
    if [int(r["frame"]) for r in rows] != list(range(115,181)):
        raise ValueError(f"{path}: GPU tags are not exactly 115..180")
    values=[float(r["gpu_command_buffer_ms"]) for r in rows]
    if any(int(r["valid"]) != 1 for r in rows) or not all(math.isfinite(v) and v > 0 for v in values):
        raise ValueError(f"{path}: invalid GPU command buffer timestamp")
    return values

def read_cpu(path, mode, items):
    with path.open(newline="") as fp:
        records=list(csv.DictReader(fp))
    if [int(r["frame"]) for r in records] != list(range(90,196)):
        raise ValueError(f"{path}: invalid CPU frame range")
    records=records[25:]
    times=[float(r["render_cpu_ms"]) for r in records]
    if not all(math.isfinite(x) and x>=0 for x in times):
        raise ValueError(f"{path}: invalid CPU time")
    for r in records:
        sub=int(r["mdi_submissions"])
        cmds=int(r["mdi_commands"])
        if mode=="direct" and (sub or cmds):
            raise ValueError(f"{path}: Direct emitted indirect commands")
        if mode=="mdi" and (sub <= 0 or not (.9*2*items <= cmds <= 2*items)):
            raise ValueError(f"{path}: failed to submit representative MDI workload")
    return times

def main():
    if len(sys.argv)!=2:
        sys.exit("Usage: batching-stage109-analyze.py CAPTURE_DIRECTORY")
    folder=pathlib.Path(sys.argv[1])
    order=("direct1","mdi1","mdi2","direct2")
    result=[]
    print("Stage 109: Direct/MDI GPU timeline vs CPU render; cross-order process controls")
    for items in (2048,4096):
        runs=[]
        for label in order:
            mode="direct" if label.startswith("direct") else "mdi"
            stem=folder/f"{items}-{label}"
            gpu=read_gpu(pathlib.Path(str(stem)+"-gpu-frames.csv"))
            cpu=read_cpu(pathlib.Path(str(stem)+"-frames.csv"),mode,items)
            row={"items":items,"trial":label,"mode":mode,"gpu_n":len(gpu),
                 "gpu_median_ms":statistics.median(gpu),"gpu_p95_ms":p95(gpu),
                 "gpu_max_ms":max(gpu),"cpu_median_ms":statistics.median(cpu),
                 "cpu_p95_ms":p95(cpu)}
            runs.append(row)
            result.append(row)
            print(f"{items} {label:7} GPU median={row['gpu_median_ms']:.5f}ms "
                  f"p95={row['gpu_p95_ms']:.5f}ms CPU median={row['cpu_median_ms']:.5f}ms")
        d=statistics.median(r["gpu_median_ms"] for r in runs if r["mode"]=="direct")
        m=statistics.median(r["gpu_median_ms"] for r in runs if r["mode"]=="mdi")
        print(f"{items} GPU median of process medians: Direct={d:.5f}ms MDI={m:.5f}ms "
              f"delta={(m/d-1)*100:+.2f}%")
    target=folder/"stage109-summary.csv"
    with target.open("w",newline="") as fp:
        writer=csv.DictWriter(fp,fieldnames=list(result[0]))
        writer.writeheader()
        writer.writerows(result)
    print(f"Saved: {target}")
    print("GPU metric is whole command-buffer timeline, not model-pass-only GPU time or FPS.")

if __name__=="__main__":
    main()
