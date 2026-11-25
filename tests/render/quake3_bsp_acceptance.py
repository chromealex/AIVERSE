"""Verify native CPU/Metal, stdio MCP and the packaged OpenArena WebGL2 viewer."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def inspect_mcp(binary, assets, work):
    project = work / 'game'
    inputs = json.loads((assets/'view.json').read_text())['items'][0]['files']
    for rel in inputs:
        dst = project / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(assets / rel, dst)
    (project / 'game.json').write_text(json.dumps({'id':'bsp-import','name':'OpenArena','maps':[]}))
    (project / 'rules.json').write_text(json.dumps({'types':{'arena':{'view':{'model':'maps/oacmpdm3.bsp'}}}}))
    operations = [('open_game', {'game':str(project)}), ('model_info', {'type':'arena'})]
    requests = [dict(jsonrpc='2.0', id=i, method='tools/call', params={'name':name,'arguments':args})
                for i, (name, args) in enumerate(operations, 1)]
    done = subprocess.run([binary, '--no-editor', '--port', '0', '--engine', ROOT],
        input=''.join(json.dumps(r)+'\n' for r in requests), capture_output=True, text=True, timeout=90,
        check=True, env=dict(os.environ, AIVERSE_TEMPLATES_DIR=str(work)))
    reply = json.loads(json.loads(done.stdout.splitlines()[-1])['result']['content'][0]['text'])
    expected = dict(surfaces=3073, lightmappedSurfaces=3034, lightmapPages=12, visibilityClusters=237)
    if reply.get('bsp') != expected or reply.get('path') != 'maps/oacmpdm3.bsp':
        raise RuntimeError(reply)
    print('MCP model_info', json.dumps(expected, sort_keys=True), flush=True)


def inspect_web(site, command, work):
    handler = partial(QuietHandler, directory=str(site))
    with ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f'http://127.0.0.1:{server.server_port}/viewer.html?src=./egyptian-trouble&lang=en'
        cmd = [str(command)] if os.access(command, os.X_OK) else ['bash', str(command)]
        cmd.append('-s=quake3-bsp-'+str(os.getpid()))
        try:
            subprocess.run([*cmd, 'open', url, '--browser=chrome'], cwd=work, check=True, capture_output=True, timeout=90)
            script = (ROOT/'demos/quake3-bsp/verify-web.js').read_text()
            done = subprocess.run([*cmd,'run-code',script], cwd=work, text=True, capture_output=True,
                                  timeout=120, check=True)
            summary = done.stdout.partition('### Ran Playwright code')[0]
            if '210000' not in summary or any(metric not in summary for metric in ['diffuse','lightmap','orbit']):
                raise RuntimeError(done.stdout+done.stderr)
            print('Chrome WebGL2', summary.strip(), flush=True)
        finally:
            subprocess.run([*cmd,'close'], cwd=work, capture_output=True, timeout=30)
            server.shutdown()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native_build', type=Path)
    parser.add_argument('web_site', type=Path)
    parser.add_argument('playwright_cli', type=Path)
    args = parser.parse_args()
    native = args.native_build.resolve()
    site = args.web_site.resolve()
    assets = site / 'egyptian-trouble'
    subprocess.run(['ctest','--test-dir',native,'-R','^quake3_bsp(_frame)?$','-V'],check=True,timeout=180,
        env=dict(os.environ,AIVERSE_OPENARENA_BSP=str(assets/'maps/oacmpdm3.bsp')))
    with tempfile.TemporaryDirectory(prefix='bsp-acceptance-') as tmp:
        inspect_mcp(native/'aiversemcp', assets, Path(tmp))
        inspect_web(site, args.playwright_cli.resolve(), Path(tmp))


if __name__ == '__main__':
    main()
