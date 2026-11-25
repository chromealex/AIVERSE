"""Stage verified Web demo assets for the static site."""
import hashlib
import re
import subprocess


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
            if hashlib.sha256(data).hexdigest() != digest:
                raise ValueError(f'checksum mismatch for {relative}. Supply its verified build with --demo-root.')
            target = out / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    if entries:
        # Git must preserve the exact bytes when the publisher stages the Pages tree.
        (out / 'demos/.gitattributes').write_text('* -text\n', encoding='ascii')
