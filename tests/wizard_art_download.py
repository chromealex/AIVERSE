import io
import sys
import tarfile
import tempfile
import unittest
import zipfile
from email.message import Message
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/wizard'))
import fetch_art


class ArtworkDownload(unittest.TestCase):
    def fetch(self, name, data, mime='application/octet-stream'):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        stage = Path(temp.name)
        url = 'https://example.invalid/' + name
        response = io.BytesIO(data)
        response.geturl = lambda: url
        response.headers = Message()
        response.headers['Content-Type'] = mime
        with patch.object(fetch_art, 'build_opener') as opener:
            opener.return_value.open.return_value = response
            fetch_art.fetch(url, stage)
        return stage / 'content'

    def test_extensionless_image(self):
        image = b'\x89PNG\r\n\x1a\n'
        self.assertEqual((self.fetch('download', image) / 'art.png').read_bytes(), image)

    def test_archives_preserve_subdirectories(self):
        for fmt in ('zip', 'tar.gz'):
            with self.subTest(fmt=fmt):
                data = io.BytesIO()
                if fmt == 'zip':
                    with zipfile.ZipFile(data, 'w') as archive:
                        archive.writestr('world/idea.md', b'Tide')
                else:
                    with tarfile.open(fileobj=data, mode='w:gz') as archive:
                        entry = tarfile.TarInfo('world/idea.md')
                        entry.size = 4
                        archive.addfile(entry, io.BytesIO(b'Tide'))
                folder = self.fetch('art.' + fmt, data.getvalue())
                self.assertEqual((folder / 'world/idea.md').read_bytes(), b'Tide')

    def test_web_page_and_broken_archive_report_errors(self):
        with self.assertRaisesRegex(ValueError, 'web page'):
            self.fetch('index.html', b'<html></html>', 'text/html')
        with self.assertRaisesRegex(ValueError, 'archive'):
            self.fetch('broken.zip', b'not an archive')


if __name__ == '__main__':
    unittest.main()
