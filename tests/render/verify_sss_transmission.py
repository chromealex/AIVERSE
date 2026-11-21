#!/usr/bin/env python3
"""Validate independently packed thin transmission, SSS and unlit fixture pixels.

ROOT contains metal-<case>.ppm and gl-<case>.ppm from the HdrScene controls.
"""
import argparse
import os
import subprocess
from pathlib import Path
from verify_sss import read_ppm


def delta(a, b):
    return sum(abs(x-y) for x, y in zip(a, b)) / len(a)


def capture(root, build):
    root.mkdir(parents=True, exist_ok=False)
    scratch = root / 'tmp'
    scratch.mkdir()
    cases = [('base', 0, 0, .1, False), ('thin', 0, .35, .1, False),
             ('thin_scale', 0, .35, .9, False), ('sss_narrow', .6, 0, .1, False),
             ('sss_wide', .6, 0, .9, False), ('combined', .6, .35, .9, False),
             ('unlit_off', 0, 0, .1, True), ('unlit_on', .85, .8, .9, True)]
    for backend, binary in [('metal', 'aiverse_hdr_scene'), ('gl', 'aiverse_hdr_gl')]:
        for name, strength, thin, scale, unlit in cases:
            label = backend + '-' + name
            env = dict(os.environ, TMPDIR=str(scratch),
                       AIVERSE_HDR_CAPTURE=str(root / (label + '.ppm')),
                       AIVERSE_SSS_TEST=str(strength),
                       AIVERSE_THIN_TRANSMISSION_TEST=str(thin),
                       AIVERSE_SSS_TEST_SCALE=str(scale))
            env.pop('AIVERSE_MATERIAL_TEST_UNLIT', None)
            if unlit:
                env['AIVERSE_MATERIAL_TEST_UNLIT'] = '1'
            with (root / (label + '.log')).open('w') as log:
                subprocess.run([str(build / binary)], env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=50)
            assert (root / (label + '.ppm')).is_file(), label
            print(label, 'captured', flush=True)


def verify(root):
    cases = ['base', 'thin', 'thin_scale', 'sss_narrow', 'sss_wide',
             'combined', 'unlit_off', 'unlit_on']
    captures = {}
    for backend in ['metal', 'gl']:
        data = {case: read_ppm(root / (backend+'-'+case+'.ppm')) for case in cases}
        assert data['thin'] == data['thin_scale'], backend+': inactive SSS scale changed thin transmission'
        assert data['unlit_off'] == data['unlit_on'], backend+': PBR factors changed fixed-light unlit shading'
        for a, b in [('base', 'thin'), ('sss_narrow', 'sss_wide'),
                     ('sss_wide', 'combined'), ('thin_scale', 'combined')]:
            changed = sum(max(abs(x-y) for x, y in zip(data[a][i:i+3], data[b][i:i+3])) > 3
                          for i in range(0, len(data[a]), 3))
            assert changed > 100, (backend, a, b, changed)
            print(backend, a, b, 'changed', changed, 'mean', round(delta(data[a], data[b]), 4))
        captures[backend] = data
    for case in cases:
        mean = delta(captures['metal'][case], captures['gl'][case])
        assert mean < 1.5, ('Metal/GL mismatch', case, mean)
        print('Metal/GL', case, 'mean', round(mean, 4))
    print('PASS: independent PBR controls; unlit compatibility; Metal/GL fixture parity')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, help='capture directory; new when --build-dir is given')
    parser.add_argument('--build-dir', type=Path, help='run freshly built Metal/GL HDR fixtures first (macOS)')
    args = parser.parse_args()
    root = args.root.resolve()
    if args.build_dir:
        capture(root, args.build_dir.resolve())
    verify(root)
