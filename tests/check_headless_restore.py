"""CLI regression: real snapshot/serialization restoration and deliberate divergence."""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='rts-restore-check-') as tmp:
    root = pathlib.Path(tmp) / 'fixture'
    shutil.copytree(repo / 'templates/skirmish', root)
    rules = root / 'rules.json'
    rules.write_text(re.sub(r'^\{', '{\n  "scripts": ["probe.lua"],', rules.read_text(), count=1, flags=re.M))
    probe = root / 'probe.lua'
    probe.write_text("on('tick',function() state.n=(state.n or 0)+1;game.set_var('restore_probe',state.n) end)")
    base = [binary, '--project', str(root), '--seed', '3', '--max-seconds', '12', '--quiet']

    def run(extra, code=0, marker=None):
        result = subprocess.run(base + extra, capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert result.returncode == code, output
        if marker:
            assert marker in output, output
        return output

    baseline = run([])
    checksum = re.search(r'^Checksum: (\w+)', baseline, re.M).group(1)
    for start in ['0', '5']:
        replay = root / ('restore-' + start + '.rpl')
        output = run(['--verify-restore-at', start, '--verify-restore-ticks', '60', '--record', str(replay)], marker='Restore OK (serialized): 60 per-tick checks')
        assert 'Restore OK (snapshot): 60 per-tick checks' in output
        assert 'Restore OK (fresh serialized): 60 per-tick checks' in output
        assert re.search(r'^Checksum: (\w+)', output, re.M).group(1) == checksum, output
        result = subprocess.run([binary, '--replay', str(replay)], capture_output=True, text=True)
        assert result.returncode == 0 and 'Replay OK' in result.stdout, result.stdout + result.stderr
    run(['--verify-restore-ticks', '0'], 2, 'requires 1..100000 ticks')
    run(['--verify-restore-at', '12'], 2, 'within the match interval')
    run(['--verify-restore-ticks', '100001'], 2, 'requires 1..100000 ticks')
    run(['--verify-restore-at', '0', '--max-seconds', '0'], 2, 'requires a normal match')
    probe.write_text("local n=0;on('tick',function() n=n+1;game.set_var('restore_probe',n) end)")
    run(['--verify-restore-at', '5', '--verify-restore-ticks', '60'], 1, 'Restore DESYNC (snapshot)')
print('Headless restoration: snapshot, serialization, initial tick, final checksum, replay, invalid ranges and deliberate divergence: OK')
