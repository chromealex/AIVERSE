"""Exercise each preset with the actual C++ schema validator and overwrite guard."""
import pathlib
import subprocess
import sys
import tempfile

engine = pathlib.Path(__file__).resolve().parents[2]
validator = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='rts-meta-presets-') as tmp:
    for preset in ('minimal', 'mobile', 'competitive', 'full'):
        project = pathlib.Path(tmp) / preset
        command = [sys.executable, str(engine / 'tools/meta/scaffold.py'), '--preset', preset, '--out', str(project)]
        if preset == "full":
            command.append("--with-ui")
        subprocess.run(command, check=True)
        for kind in ('economy', 'boards', 'chats'):
            subprocess.run([str(validator), kind, str(project / 'meta' / (kind + '.json'))], check=True)
        subprocess.run([sys.executable, str(engine/'tools/meta/compose.py'), '--project', str(project), '--validator', str(validator)], check=True)
        if preset == 'full':
            assert (project/'ui/meta/app.js').is_file()
            for operation in ('check', 'pin', 'verify'):
                subprocess.run([sys.executable, str(engine/'tools/meta/catalog.py'), operation, '--project', str(project)], check=True)
            generated = (project/'meta/service.generated.json').read_bytes()
            original = (project/'meta/extensions.json').read_bytes()
            (project/'meta/extensions.json').write_text('{"unknown":1}')
            invalid = subprocess.run([sys.executable, str(engine/'tools/meta/compose.py'), '--project', str(project), '--validator', str(validator)], capture_output=True)
            assert invalid.returncode and (project/'meta/service.generated.json').read_bytes() == generated
            (project/'meta/extensions.json').write_bytes(original)
        before = {p.name: p.read_bytes() for p in (project / 'meta').iterdir()}
        result = subprocess.run(command, capture_output=True)
        if result.returncode == 0:
            raise RuntimeError('scaffold overwrote an existing project')
        after = {p.name: p.read_bytes() for p in (project / 'meta').iterdir()}
        if before != after:
            raise RuntimeError('failed scaffold changed files')
print('All presets validated; overwrite guard passed')
