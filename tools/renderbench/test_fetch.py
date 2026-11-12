import unittest
from fetch_sponza import safe_name,digest
class FetchTests(unittest.TestCase):
    def test_path(self):
        self.assertEqual(str(safe_name('main_sponza/textures/a.png')),'main_sponza/textures/a.png')
        for name in ['/tmp/a','../a','main/../../a','main\\a']:
            with self.assertRaises(ValueError):safe_name(name)
    def test_hash(self):
        self.assertEqual(digest(b''),'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')
if __name__=='__main__':unittest.main()
