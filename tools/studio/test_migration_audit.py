import tempfile
import unittest
from pathlib import Path

from migration_audit import scan


class StudioMigrationAuditTests(unittest.TestCase):
    def test_counts_only_legacy_ui_and_tracks_native_boundary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            window = root / "apps/editor/windows/SceneWindow.cpp"
            window.parent.mkdir(parents=True)
            window.write_text('#include "imgui.h"\nvoid draw(){ ImGui::Text("x"); }\n')
            native = root / "apps/studio-native/Session.cpp"
            native.parent.mkdir(parents=True)
            native.write_text('void tick() {}\n')
            web = root / "apps/studio-web/src/components/Panel.tsx"
            web.parent.mkdir(parents=True)
            web.write_text('export default function Panel() { return null; }\n')
            r = scan(root)
            self.assertEqual(r["legacy_window_cpp_files"], 1)
            self.assertEqual(r["legacy_imgui_calls"], 1)
            self.assertEqual(r["legacy_imgui_source_files"], 1)
            self.assertEqual(len(r["new_react_components"]), 1)
            self.assertFalse(r["new_native_imgui_violations"])
            self.assertFalse(r["ready_to_remove_editor_imgui"])
            native.write_text('#include "imgui.h"\n')
            r = scan(root)
            self.assertEqual(r["new_native_imgui_violations"], ["apps/studio-native/Session.cpp"])

    def test_empty_project(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = scan(Path(tmp))
            self.assertEqual(result["legacy_window_cpp_files"], 0)
            self.assertEqual(result["legacy_imgui_calls"], 0)
            self.assertEqual(result["new_native_imgui_violations"], [])


if __name__ == "__main__":
    unittest.main()
