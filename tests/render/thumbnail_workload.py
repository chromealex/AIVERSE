"""Measure end-to-end thumbnail captures using a closed, indexed torus-knot mesh."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

from headless_model_capture import decoded_rows


def knot(t):
    radius = 1.35 + .5 * math.cos(3*t)
    return (radius*math.cos(2*t), radius*math.sin(2*t), .5*math.sin(3*t))


def unit(v):
    length = math.sqrt(sum(x*x for x in v))
    return tuple(x/length for x in v)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def write_model(path, rings=256, sides=32):
    lines = ['mtllib knot.mtl', 'usemtl ceramic']
    (path.parent/'knot.mtl').write_text('newmtl ceramic\nKd 0.18 0.72 0.91\n')
    for i in range(rings):
        t = math.tau*i/rings
        center = knot(t)
        tangent = unit(tuple(b-a for a, b in zip(knot(t-.0001), knot(t+.0001))))
        axis = unit(cross(tangent, (0, 0, 1)))
        other = cross(tangent, axis)
        for j in range(sides):
            phi = math.tau*j/sides
            n = tuple(math.cos(phi)*a + math.sin(phi)*b for a, b in zip(axis, other))
            p = tuple(c+.22*d for c,d in zip(center,n))
            lines.append('v %.7f %.7f %.7f' % p)
            lines.append('vn %.7f %.7f %.7f' % n)
    for i in range(rings):
        for j in range(sides):
            indices = [1+(r%rings)*sides+s%sides for r,s in
                       [(i,j),(i,j+1),(i+1,j+1),(i+1,j)]]
            for tri in (indices[:3], [indices[0], *indices[2:]]):
                lines.append('f ' + ' '.join(f'{k}//{k}' for k in tri))
    path.write_text('\n'.join(lines)+'\n')
    return 2*rings*sides


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('backends', nargs='+')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    model = args.output/'knot.obj'
    triangles = write_model(model)
    results = []
    for backend in args.backends:
        png = args.output/(backend+'.png')
        command = [args.executable, '--backend', backend, model, png]
        subprocess.run(command, check=True)
        durations = []
        for _ in range(5):
            start = time.perf_counter()
            subprocess.run(command, check=True)
            durations.append(round(1000*(time.perf_counter()-start), 2))
        rows = list(decoded_rows(png))
        bg = rows[0][:4]
        foreground = sum(row[i:i+4] != bg for row in rows for i in range(0,len(row),4))
        if not 10000 < foreground < 250000:
            raise ValueError(f'{backend}: unexpected foreground area {foreground}')
        results.append(dict(backend=backend, triangles=triangles, width=512, height=512,
                            samples_ms=durations, median_ms=statistics.median(durations),
                            foreground=foreground, sha256=hashlib.sha256(png.read_bytes()).hexdigest()))
    data = json.dumps(results, indent=2)
    print(data)
    (args.output/'results.json').write_text(data+'\n')


if __name__ == '__main__':
    main()
