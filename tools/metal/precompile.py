#!/usr/bin/env python3
"""Build packaged Metal IR libraries and a host-GPU pipeline archive from a real client.
Run after linking, before signing/packing. Requires Xcode Metal Toolchain and a GUI session.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def run(command, log, **kwargs):
    with log.open('w') as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--content', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--map', default='0')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    executable, content, work = args.executable.resolve(), args.content.resolve(), args.work_dir.resolve()
    # Retain every build's capture and compiler logs; never reuse an old scenario silently.
    work.mkdir(parents=True, exist_ok=True)
    work = work/('run-'+str(time.time_ns()))
    work.mkdir()
    sources, overlay = work/'sources', work/'content'
    sources.mkdir(); overlay.mkdir()
    for item in content.iterdir():
        if item.name == 'engine':
            shutil.copytree(item, overlay/item.name, symlinks=True, ignore=shutil.ignore_patterns('metal'))
        else:
            (overlay/item.name).symlink_to(item, target_is_directory=item.is_dir())
    libraries = overlay/'engine/metal'; libraries.mkdir(parents=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith(('AIVERSE_', 'KINGSTON_'))}
    env.update(AIVERSE_CONTENT=str(content), AIVERSE_SEED='7', AIVERSE_SPLASH='off', AIVERSE_BENCH_STARTUP='1')
    command = [str(executable), '--map', args.map, '--size', '1280x800']
    capture_env = dict(env, AIVERSE_METAL_SOURCE_CAPTURE=str(sources), AIVERSE_METAL_PRECOMPILED='off')
    print('Capturing actual startup shader variants...', flush=True)
    run(command, work/'capture-sources.log', env=capture_env, cwd=content, timeout=180)
    files = sorted(sources.glob('*.metal'))
    if not files:
        raise RuntimeError('Client produced no Metal source capture; rebuild with MetalShaderAssets support')
    def compile_one(source):
        if hashlib.sha256(source.read_bytes()).hexdigest() != source.stem:
            raise RuntimeError('Source identity mismatch: '+source.name)
        air = work/(source.stem+'.air')
        # Match the runtime graphics path: fast math, Metal 3.0; no game-specific defines here.
        run(['xcrun','-sdk','macosx','metal','-std=metal3.0','-mmacosx-version-min=13.0',
             '-ffast-math','-c',str(source),'-o',str(air)],work/(source.stem+'.compile.log'),timeout=120)
        run(['xcrun','-sdk','macosx','metallib',str(air),'-o',str(libraries/(source.stem+'.metallib'))],
            work/(source.stem+'.link.log'),timeout=60)
        air.unlink()
    print('Compiling', len(files), 'unique libraries...', flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        list(pool.map(compile_one, files))
    # Archive must be built from these exact offline libraries, not runtime-compiled functions.
    archive_env = dict(env, AIVERSE_CONTENT=str(overlay), AIVERSE_METAL_REQUIRE_PRECOMPILED='1',
                       AIVERSE_METAL_ARCHIVE_CAPTURE=str(libraries/'pipelines.metallib'))
    print('Building host-GPU pipeline archive from packaged libraries...', flush=True)
    run(command, work/'capture-pipelines.log', env=archive_env, cwd=overlay, timeout=240)
    log = (work/'capture-pipelines.log').read_text()
    if 'sourceMisses=0' not in log or 'pipelineFailures=0' not in log or any(x in log for x in ('archive export failed','archive capture failed','required library missing','invalid library','model shader:','model pipeline:')):
        raise RuntimeError('Pipeline capture failed validation; inspect '+str(work/'capture-pipelines.log'))
    if not (libraries/'pipelines.metallib').is_file():
        raise RuntimeError('Pipeline archive was not serialized')
    verify_env = dict(env, AIVERSE_CONTENT=str(overlay), AIVERSE_METAL_REQUIRE_PRECOMPILED='1')
    print('Verifying with source compilation and archive misses forbidden...', flush=True)
    run(command, work/'verify.log', env=verify_env, cwd=overlay, timeout=120)
    log = (work/'verify.log').read_text()
    if 'sourceMisses=0' not in log or 'pipelineFailures=0' not in log or 'archive=1 strict=1' not in log or any(x in log for x in ('missing','failed','invalid library','model pipeline:','model shader:')):
        raise RuntimeError('Strict verification failed; inspect '+str(work/'verify.log'))
    manifest = {'version':1, 'platform':'macos', 'minimumOS':'13.0', 'language':'metal3.0',
                'pipelineArchive':'host GPU; other GPUs/OS versions may compile pipelines from packaged IR',
                'libraries':len(files), 'executableSha256':hashlib.sha256(executable.read_bytes()).hexdigest(),
                'compiler':subprocess.check_output(['xcrun','metal','--version'],text=True).strip(),
                'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(libraries.glob('*.metallib'))}}
    (libraries/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    output = content/'engine/metal'
    staged = content/'engine'/('metal-staged-'+str(time.time_ns()))
    shutil.copytree(libraries, staged)
    if output.exists(): output.rename(work/'previous-packaged-metal')
    staged.rename(output)
    print('Packaged',len(files),'libraries and pipeline archive:',output,flush=True)


if __name__ == '__main__':
    main()
