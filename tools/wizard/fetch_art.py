"""Download artwork for the game wizard into its newly reserved import directory."""
import argparse
import json
import re
import stat
import sys
import tarfile
import time
import zipfile
from pathlib import Path, PurePosixPath, PureWindowsPath
from urllib.parse import parse_qsl, unquote, urlencode, urlsplit, urlunsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

MIB = 1024 * 1024
MAX_FILE = 300 * MIB
MAX_TOTAL = 1024 * MIB
MAX_ENTRIES = 5000


def http_url(url):
    parts = urlsplit(url)
    if parts.scheme not in ('http', 'https') or not parts.hostname:
        raise ValueError('Enter an HTTP or HTTPS artwork link.')
    return parts


class HttpRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        http_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def direct_url(url):
    parts = http_url(url)
    if parts.hostname in ('dropbox.com', 'www.dropbox.com'):
        query = [(k, '1' if k == 'dl' else v) for k, v in parse_qsl(parts.query, keep_blank_values=True)]
        return urlunsplit(parts._replace(query=urlencode(query)))
    if parts.hostname == 'drive.google.com':
        match = re.match(r'/file/d/([a-zA-Z0-9_-]+)(/|$)', parts.path)
        if match:
            return 'https://drive.google.com/uc?export=download&id=' + match[1]
    return url


def write_stream(src, target, limit, deadline=None):
    size = 0
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as dst:
        while chunk := src.read(MIB):
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError('Artwork download timed out.')
            size += len(chunk)
            if size > limit:
                raise ValueError('The artwork exceeds the import size limit.')
            dst.write(chunk)
    return size


def member_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or any(
        ':' in part or part.endswith(('.', ' ')) or PureWindowsPath(part).is_reserved()
        for part in path.parts
    ):
        raise ValueError('Archive contains an invalid file name.')
    return path


def unpack(entries, target):
    total = 0
    files = 0
    for i, (name, is_dir, regular, size, open_entry) in enumerate(entries, 1):
        if i > MAX_ENTRIES:
            raise ValueError('The archive has too many entries.')
        path = target / member_path(name)
        if is_dir:
            path.mkdir(parents=True, exist_ok=True)
            continue
        if not regular:
            raise ValueError('Artwork archives must contain regular files and folders.')
        if size > min(MAX_FILE, MAX_TOTAL - total):
            raise ValueError('The artwork exceeds the import size limit.')
        with open_entry() as src:
            total += write_stream(src, path, size)
        files += 1
    if not files:
        raise ValueError('The artwork archive is empty.')


def fetch(url, stage):
    url = direct_url(url)
    target = stage / 'content'
    target.mkdir()
    payload = stage / 'download'
    req = Request(url, headers={'User-Agent': 'AIVERSE-Studio'})
    deadline = time.monotonic() + 600
    with build_opener(HttpRedirects).open(req, timeout=60) as response:
        name = PurePosixPath(unquote(urlsplit(response.geturl()).path)).name or 'art'
        content_type = response.headers.get_content_type()
        write_stream(response, payload, MAX_TOTAL, deadline)
    with payload.open('rb') as stream:
        head = stream.read(512)
    if not head:
        raise ValueError('The artwork download is empty.')
    if zipfile.is_zipfile(payload):
        with zipfile.ZipFile(payload) as archive:
            unpack(((e.filename, e.is_dir(), stat.S_IFMT(e.external_attr >> 16) in (0, stat.S_IFREG),
                     e.file_size, lambda: archive.open(e)) for e in archive.infolist()), target)
    elif tarfile.is_tarfile(payload):
        with tarfile.open(payload, 'r:*') as archive:
            unpack(((e.name, e.isdir(), e.isfile(), e.size, lambda: archive.extractfile(e))
                    for e in archive), target)
    else:
        if name.lower().endswith(('.zip', '.tar', '.tgz', '.tar.gz')):
            raise ValueError('The downloaded archive could not be opened.')
        if content_type == 'text/html' or head.lstrip().lower().startswith((b'<!doctype html', b'<html')):
            raise ValueError('The link returned a web page. Choose a direct file or archive link.')
        if payload.stat().st_size > MAX_FILE:
            raise ValueError('The artwork exceeds the per-file size limit.')
        name = re.sub(r'[^\w.-]', '_', name)
        if not Path(name).suffix:
            signatures = [(b'\x89PNG', '.png'), (b'\xff\xd8', '.jpg'), (b'OggS', '.ogg'),
                          (b'RIFF', '.wav'), (b'ID3', '.mp3'), (b'glTF', '.glb')]
            name = 'art' + next((ext for signature, ext in signatures if head.startswith(signature)), '')
        payload.rename(target / member_path(name))
    payload.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--json', action='store_true')
    parser.add_argument('request', type=Path)
    args = parser.parse_args()
    try:
        request = json.loads(args.request.read_text(encoding='utf-8'))
        fetch(request['url'], args.request.parent)
        print(json.dumps({'result': {}}))
    except Exception as exc:
        print(json.dumps({'error': str(exc)}))
        sys.exit(1)
