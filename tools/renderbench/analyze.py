#!/usr/bin/env python3
"""Summarize raw renderbench reports; compare only matching workloads and GPU models."""
import argparse
import json
import math
import statistics
from pathlib import Path


def distribution(values):
    if len(values) < 30 or any(not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in values):
        raise ValueError('need at least 30 finite, nonnegative samples per metric/camera')
    return dict(count=len(values), meanMs=statistics.mean(values), medianMs=statistics.median(values),
                p95Ms=statistics.quantiles(values, n=100, method='inclusive')[94], peakMs=max(values))


def summarize(report):
    if not report.get('complete') or report.get('error'):
        raise ValueError('incomplete or failed benchmark')
    cameras = {}
    for row in report['frames']:
        cameras.setdefault(row['camera'], []).append(row)
    result = {}
    for camera, rows in cameras.items():
        if len({r['sample'] for r in rows}) != len(rows):
            raise ValueError('duplicate camera/sample')
        result[camera] = {metric: distribution([r[metric] for r in rows]) for metric in ('cpuEncodeMs', 'cpuFrameMs')}
        gpu = [r['gpuMs'] for r in rows if r.get('gpuMs') is not None]
        result[camera]['gpuMs'] = distribution(gpu) if len(gpu) == len(rows) else None
        result[camera]['drawCalls'] = sorted(set(r['drawCalls'] for r in rows))
    return result


def compare(a, b, sa, sb):
    # CPU frame wall time includes platform pacing; never rank it across presentation paths.
    for key in ('scene', 'profile', 'renderSettings', 'hardware', 'hostId'):
        if not a.get(key) or a.get(key) != b.get(key):
            raise ValueError(f'incompatible {key}; comparison refused')
    if a.get('assetVerification') != 'sha256' or b.get('assetVerification') != 'sha256':
        raise ValueError('run through run.py to verify the assets before comparison')
    if a.get('assetWarnings') or b.get('assetWarnings'):
        raise ValueError('asset warnings present; resolve fidelity issues before comparing engines')
    if sa.keys() != sb.keys():
        raise ValueError('camera sets differ')
    result = {}
    for c in sa:
        ga, gb = sa[c]['gpuMs'], sb[c]['gpuMs']
        if not ga or not gb or ga['medianMs'] <= 0:
            raise ValueError('complete GPU timing required for comparison')
        result[c] = dict(gpuMedianRatioBoverA=gb['medianMs']/ga['medianMs'],
                         gpuP95RatioBoverA=gb['p95Ms']/ga['p95Ms'])
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reports', nargs='+', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    reports = [json.loads(path.read_text()) for path in args.reports]
    summaries = [summarize(r) for r in reports]
    out = dict(percentile='statistics.quantiles inclusive (linear interpolation)',
               runs=[dict(file=str(path), engine=r['engine'], device=r['device'], profile=r['profile'], cameras=s)
                     for path, r, s in zip(args.reports, reports, summaries)])
    if len(reports) == 2:
        out['comparison'] = compare(*reports, *summaries)
    text = json.dumps(out, indent=2)
    if args.output:
        args.output.write_text(text+'\n')
    print(text)


if __name__ == '__main__':
    main()
