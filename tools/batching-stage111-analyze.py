#!/usr/bin/env python3
"""Stage 111: compare Metal shadow/opaque GPU counter *ticks* by render path.

MTLCounterResultTimestamp reports raw GPU clock timestamps. Tick differences
must not be called milliseconds without a separate clock calibration. CPU and
command-buffer GPU timestamps remain separate, independent diagnostics.
"""
import csv
import math
import pathlib
import statistics
import sys

def p95(values):
    return sorted(values)[math.ceil(.95 * len(values))-1]

def load(path, expected_fields, first, last):
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        if reader.fieldnames != expected_fields:
            raise ValueError(f"{path}: wrong header {reader.fieldnames}")
        rows = list(reader)
    if [int(r["frame"]) for r in rows] != list(range(first, last+1)):
        raise ValueError(f"{path}: expected contiguous frames {first}..{last}")
    return rows

def analyze_run(stem, items, mode):
    counters = load(pathlib.Path(str(stem)+"-model-gpu-counters.csv"),
                    ["frame","shadow_gpu_ticks","opaque_gpu_ticks","valid"],115,180)
    cb = load(pathlib.Path(str(stem)+"-gpu-frames.csv"),
              ["frame","gpu_command_buffer_ms","valid"],115,180)
    with pathlib.Path(str(stem)+"-frames.csv").open(newline="") as fp:
        cpu=list(csv.DictReader(fp))
    if [int(r["frame"]) for r in cpu] != list(range(90,196)):
        raise ValueError(f"{stem}: wrong CPU timeline frame sequence")
    cpu=cpu[25:]
    for r in cpu:
        submissions=int(r["mdi_submissions"]); commands=int(r["mdi_commands"])
        if mode=="direct" and (submissions or commands):
            raise ValueError(f"{stem}: forced Direct unexpectedly submitted MDI")
        if mode=="mdi" and (submissions<=0 or not (.90*2*items<=commands<=2*items)):
            raise ValueError(f"{stem}: forced MDI not exercised")
    def validate(values):
        if any(not math.isfinite(v) or v<=0 for v in values):
            raise ValueError(f"{stem}: missing/invalid GPU counter or timestamp")
        return values
    if any(int(r["valid"])!=1 for r in counters+cb):
        raise ValueError(f"{stem}: invalid GPU completion")
    shadow=validate([float(r["shadow_gpu_ticks"]) for r in counters])
    opaque=validate([float(r["opaque_gpu_ticks"]) for r in counters])
    whole=validate([float(r["gpu_command_buffer_ms"]) for r in cb])
    wall=validate([float(r["render_cpu_ms"]) for r in cpu])
    return {"items":items,"mode":mode,"gpu_n":len(counters),
            "shadow_ticks_median":statistics.median(shadow),
            "shadow_ticks_p95":p95(shadow),
            "opaque_ticks_median":statistics.median(opaque),
            "opaque_ticks_p95":p95(opaque),
            "gpu_cb_ms_median":statistics.median(whole),
            "gpu_cb_ms_p95":p95(whole),
            "cpu_render_ms_median":statistics.median(wall),
            "cpu_render_ms_p95":p95(wall)}

def main():
    if len(sys.argv)!=2:
        sys.exit("Usage: batching-stage111-analyze.py CAPTURE_DIR")
    folder=pathlib.Path(sys.argv[1])
    results=[]
    for items in (2048,4096):
        for mode in ("direct","mdi"):
            row=analyze_run(folder/f"{items}-{mode}",items,mode)
            results.append(row)
        direct, mdi=results[-2:]
        def change(key):
            return (mdi[key]/direct[key]-1.)*100.
        print(f"{items}: GPU shadow ticks Direct={direct['shadow_ticks_median']:.0f} "
              f"MDI={mdi['shadow_ticks_median']:.0f} change={change('shadow_ticks_median'):+.2f}%")
        print(f"{items}: GPU opaque ticks Direct={direct['opaque_ticks_median']:.0f} "
              f"MDI={mdi['opaque_ticks_median']:.0f} change={change('opaque_ticks_median'):+.2f}%")
        print(f"{items}: whole GPU CB Direct={direct['gpu_cb_ms_median']:.4f}ms "
              f"MDI={mdi['gpu_cb_ms_median']:.4f}ms change={change('gpu_cb_ms_median'):+.2f}%")
    output=folder/"stage111-summary.csv"
    with output.open("w",newline="") as fp:
        writer=csv.DictWriter(fp,fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    print(f"Report: {output}")
    print("IMPORTANT: shadow/opaque counter differences are GPU clock TICKS, NOT milliseconds.")
    print("Sampling can perturb performance; compare both modes with sampling enabled.")

if __name__=="__main__":
    main()
