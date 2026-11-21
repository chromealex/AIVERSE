"""Project content layout integration points must remain wired across clients."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ContentProjectLayoutTests(unittest.TestCase):
    def test_new_projects_create_content_directory_after_template_copy(self):
        source=(ROOT/"apps/editor/core/Projects.cpp").read_text(encoding="utf-8")
        method=source[source.index("std::string createProject("):]
        self.assertIn('const fs::path content = dst / "content";',method)
        self.assertIn('fs::create_directories(content, ec);',method)
        self.assertIn('fs::is_symlink(contentStatus)',method)
        self.assertGreater(method.index('const fs::path content = dst / "content";'),
                           method.index('if (templateId != "empty")'))
        self.assertLess(method.index('fs::create_directories(content, ec);'),
                        method.index('Json m = Json::parseFile'))

    def test_existing_project_opens_ensure_directory_in_studio(self):
        source=(ROOT/"apps/studio-native/StudioSession.cpp").read_text(encoding="utf-8")
        self.assertGreaterEqual(source.count('studioContentRoot(ws_.project.dir)'),2)
        self.assertIn('StudioAssetCatalog catalog(ws_.project.dir)',source)
        self.assertIn('catalog.rebuild()',source)
        self.assertIn('catalog.all()',source)
        self.assertNotIn('ws_.call("asset_list", Json::object()',source)

    def test_content_is_not_a_new_project_manifest_schema(self):
        source=(ROOT/"apps/editor/core/Projects.cpp").read_text(encoding="utf-8")
        self.assertIn('Json m = Json::parseFile((dst / "game.json").string());',source)
        self.assertNotIn('m.set("content"',source)

if __name__ == "__main__":
    unittest.main()
