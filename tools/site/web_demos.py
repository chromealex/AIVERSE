"""Stage verified Web demo assets for the static site."""
import hashlib
import re
import subprocess


def versioned_index(html, files):
    """Pin the page to its own runtime files and say what failed.

    The file names stay index.js / index.wasm / index.data on every publish, and browsers (Safari
    above all) may keep an older copy for a while: a new page with an old runtime cannot start.
    The query is the digest of the verified files, so a republish always loads a matching set.
    """
    version = hashlib.sha256(''.join(files[k] for k in sorted(files)).encode()).hexdigest()[:12]
    text = html.decode('utf-8')
    text = re.sub(r'(<script[^>]*\ssrc=)(["\']?)index\.js\2', rf'\1\2index.js?v={version}\2', text, count=1)
    text = text.replace('var Module={', f'var Module={{locateFile:function(p){{return p+"?v={version}"}},', 1)
    # Show the error itself instead of a pointer to the console.
    text = text.replace('window.onerror=function(){Module.setStatus("Error: see the browser console")}',
                        'window.onerror=function(m){Module.setStatus("Error: "+(m||"see the browser console"))}', 1)
    return text.encode('utf-8')


def unversioned_index(html):
    """Inverse of versioned_index for a page that is already published on gh-pages."""
    text = html.decode('utf-8')
    text = re.sub(r'(index\.js)\?v=[0-9a-f]{12}', r'\1', text, count=1)
    text = re.sub(r'var Module=\{locateFile:function\(p\)\{return p\+"\?v=[0-9a-f]{12}"\},', 'var Module={', text, count=1)
    text = text.replace('window.onerror=function(m){Module.setStatus("Error: "+(m||"see the browser console"))}',
                        'window.onerror=function(){Module.setStatus("Error: see the browser console")}', 1)
    return text.encode('utf-8')


def copy_web_demos(entries, out, demo_root, repo):
    """Copy only declared assets. The checked-in digests pin the tested browser build."""
    for slug, files in entries.items():
        if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', slug) or 'index.html' not in files:
            raise ValueError(f'invalid demo or missing index.html: {slug}')
        source = demo_root / slug if demo_root else None
        for name, digest in files.items():
            parts = name.split('/')
            if (not all(re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9._-]*', part) for part in parts)
                    or not re.fullmatch(r'[a-f0-9]{64}', digest)):
                raise ValueError(f'invalid demo asset: {slug}/{name}')
            relative = f'demos/{slug}/{name}'
            if source and source.exists():
                data = (source / name).read_bytes()
            else:
                previous = subprocess.run(['git', 'cat-file', 'blob', f'origin/gh-pages:{relative}'],
                                          cwd=repo, capture_output=True)
                if previous.returncode:
                    raise ValueError(f'{relative} is missing. Supply its verified build with --demo-root.')
                data = previous.stdout
                if name == 'index.html':
                    data = unversioned_index(data)
            if hashlib.sha256(data).hexdigest() != digest:
                raise ValueError(f'checksum mismatch for {relative}. Supply its verified build with --demo-root.')
            if name == 'index.html':
                data = versioned_index(data, files)
            target = out / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    if entries:
        # Git must preserve the exact bytes when the publisher stages the Pages tree.
        (out / 'demos/.gitattributes').write_text('* -text\n', encoding='ascii')
