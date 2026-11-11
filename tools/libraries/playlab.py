#!/usr/bin/env python3
"""Run reproducible headless cases; preserve logs and label time limits separately from draws."""
import argparse
import json
import re
import subprocess
import time
from pathlib import Path

def run_cases(headless, cases, output):
    output.mkdir(parents=True, exist_ok=True)
    results = []
    ids = set()
    for case in cases:
        if set(case) - {'id', 'project', 'seed', 'seconds', 'timeout'}:
            raise ValueError('unknown case field')
        name = case.get('id')
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', name) or name in ids:
            raise ValueError('invalid or duplicate case id')
        ids.add(name)
        for key, default in [('seed', 3), ('seconds', 1800), ('timeout', 300)]:
            value = case.get(key, default)
            if type(value) is not int or value < (0 if key == 'seed' else 1) or value > 2147483647:
                raise ValueError('invalid ' + key)
        if not isinstance(case.get('project'), str) or not (Path(case['project']) / 'game.json').is_file():
            raise ValueError('invalid project')
    for case in cases:
        name = case['id']
        command = [str(headless), '--project', str(Path(case['project']).resolve()), '--seed', str(case.get('seed', 3)), '--max-seconds', str(case.get('seconds', 1800))]
        started = time.monotonic()
        row = dict(id=name, command=command, status='error')
        try:
            completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=case.get('timeout', 300))
            log = completed.stdout
            row['exitCode'] = completed.returncode
            match = re.search(r'^Result: winner=(.*?)  ([0-9]+) game-s.*?sim=([0-9]+) ms', log, re.M)
            checksum = re.search(r'^Checksum: ([0-9a-f]+)', log, re.M)
            if completed.returncode == 0 and match and checksum:
                winner = match.group(1)
                row.update(status='time-limit' if winner.startswith('none') else 'draw' if winner == 'draw' else 'finished', winner=winner, gameSeconds=int(match.group(2)), simMilliseconds=int(match.group(3)), checksum=checksum.group(1))
        except subprocess.TimeoutExpired as error:
            row['status'] = 'timeout'
            log = error.stdout or ''
            if isinstance(log, bytes):
                log = log.decode('utf-8', errors='replace')
        row['wallSeconds'] = round(time.monotonic() - started, 3)
        (output / (name + '.log')).write_text(log)
        results.append(row)
        # Save a checkpoint after every case, so interrupted runs remain inspectable.
        temporary = output / 'results.json.tmp'
        temporary.write_text(json.dumps(results, indent=2) + '\n')
        temporary.replace(output / 'results.json')
    return results

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--headless', required=True, type=Path)
    parser.add_argument('--cases', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    cases = json.loads(args.cases.read_text())
    if not isinstance(cases, list) or not cases:
        parser.error('cases must be a nonempty array')
    results = run_cases(args.headless.resolve(), cases, args.output)
    print(json.dumps(results, indent=2))
    return 0 if all(r['status'] in ('finished', 'draw') for r in results) else 1

if __name__ == '__main__':
    raise SystemExit(main())
