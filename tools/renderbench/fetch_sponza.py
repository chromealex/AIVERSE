#!/usr/bin/env python3
"""Fetch Intel Sponza's glTF dependencies via ZIP ranges; retain credits and content hashes."""
import argparse
import hashlib
import io
import json
import shutil
import urllib.parse
import urllib.request
import urllib.error
import time
import zipfile
from pathlib import Path, PurePosixPath

URL = 'https://cdrdv2.intel.com/v1/dl/getContent/830833'
MODEL = 'main_sponza/NewSponza_Main_glTF_003.gltf'

class RemoteZip(io.RawIOBase):
    def __init__(self, source=URL):
        self.source = source
        with urllib.request.urlopen(urllib.request.Request(source+'?refresh='+str(time.time_ns()), method='HEAD'), timeout=60) as r:
            self.url, self.size = r.url, int(r.headers['Content-Length'])
            self.etag = r.headers.get('ETag', '')
        self.pos = 0
    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos
    def seek(self, offset, whence=0):
        self.pos = offset if whence == 0 else self.pos + offset if whence == 1 else self.size + offset
        return self.pos
    def read(self, n=-1):
        n = min(self.size-self.pos, n if n >= 0 else self.size)
        if n <= 0: return b''
        request = urllib.request.Request(self.url, headers={'Range': f'bytes={self.pos}-{self.pos+n-1}'})
        try:
            response = urllib.request.urlopen(request, timeout=120)
        except urllib.error.HTTPError as e:
            if e.code != 403: raise
            # Intel's redirect is signed and expires during a multi-GB download.
            with urllib.request.urlopen(urllib.request.Request(self.source+'?refresh='+str(time.time_ns()), method='HEAD'), timeout=60) as fresh:
                if int(fresh.headers['Content-Length']) != self.size or fresh.headers.get('ETag','') != self.etag:
                    raise RuntimeError('archive changed during download')
                self.url = fresh.url
            request = urllib.request.Request(self.url, headers={'Range': f'bytes={self.pos}-{self.pos+n-1}'})
            response = urllib.request.urlopen(request, timeout=120)
        with response as r:
            if r.status != 206: raise RuntimeError('server ignored Range; refusing full archive')
            expected = f'bytes {self.pos}-{self.pos+n-1}/{self.size}'
            if r.headers.get('Content-Range') != expected: raise RuntimeError('inconsistent archive range')
            data = r.read()
        if len(data) != n: raise RuntimeError('truncated range')
        self.pos += len(data)
        return data

def digest(data): return hashlib.sha256(data).hexdigest()

def safe_name(name):
    p=PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or '\\' in name: raise ValueError('unsafe archive path')
    return p

def fetch(dest):
    dest.mkdir(parents=True, exist_ok=True)
    remote = RemoteZip()
    with zipfile.ZipFile(remote) as z:
        model_data = z.read(MODEL)
        gltf = json.loads(model_data)
        names = [MODEL, 'main_sponza/credits_license.txt']
        for item in gltf.get('buffers', []) + gltf.get('images', []):
            uri = item.get('uri', '')
            if uri.startswith('data:'): continue
            if not uri or urllib.parse.urlsplit(uri).scheme: raise ValueError('external/empty resource URI')
            names.append(str(PurePosixPath(MODEL).parent / safe_name(urllib.parse.unquote(uri))))
        names = sorted(set(names))
        total = sum(z.getinfo(n).file_size for n in names)
        missing = sum(z.getinfo(n).file_size for n in names if not (dest/n).exists())
        print(f'{len(names)} files, {total/1e9:.2f} GB; {missing/1e9:.2f} GB missing', flush=True)
        if shutil.disk_usage(dest).free < missing + 200*1024*1024: raise RuntimeError('insufficient free disk space')
        records = []
        for i,name in enumerate(names):
            safe_name(name); target=dest/name
            info=z.getinfo(name)
            # Existing files are only reused when CRC and byte count match this pinned archive.
            if target.exists():
                import zlib
                data=target.read_bytes()
                if len(data)!=info.file_size or zlib.crc32(data)!=info.CRC: raise RuntimeError(f'modified file: {target}')
            else:
                data = model_data if name == MODEL else z.read(name)
                target.parent.mkdir(parents=True,exist_ok=True)
                temp=target.with_suffix(target.suffix+'.part');temp.write_bytes(data);temp.replace(target)
            records.append(dict(path=name,bytes=len(data),sha256=digest(data)))
            print(f'{i+1}/{len(names)} {name}',flush=True)
        fingerprint=digest(json.dumps(records,sort_keys=True,separators=(',',':')).encode())
        manifest=dict(source=URL,archiveBytes=remote.size,archiveETag=remote.etag,model=MODEL,
                      license='See main_sponza/credits_license.txt (CC-BY-4.0 and source introductory terms)',files=records,fingerprint=fingerprint)
        lock=Path(__file__).with_name('sponza-lock.json')
        if lock.exists() and json.loads(lock.read_text())['fingerprint']!=fingerprint:
            raise RuntimeError('upstream asset differs from repository lock; do not compare it as the same workload')
        (dest/'asset-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        recipe=json.loads(Path(__file__).with_name('scene.json').read_text())
        recipe['model']=MODEL;recipe['assetFingerprint']=fingerprint
        (dest/'scene.json').write_text(json.dumps(recipe,indent=2)+'\n')
        print(f'Ready: {dest / "scene.json"}',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('destination',type=Path);a=p.parse_args();fetch(a.destination)
