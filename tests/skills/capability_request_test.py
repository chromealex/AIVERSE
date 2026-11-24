import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("capability_request", ROOT / "tools/feedback/capability_request.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CapabilityRequestTest(unittest.TestCase):
    def setUp(self):
        self.data = dict(title="Shared placement snapping", need="Snap placed entities to surfaces.",
                         gap="No shared placement snap API.", existing="Entity transforms exist.",
                         evidence="docs/capability-status.json: E1")

    def test_load_and_render(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "draft.json"
            path.write_text(json.dumps(self.data), encoding="utf-8")
            self.assertEqual(module.load_draft(path), self.data)
            self.assertIn("### Current gap\nNo shared placement snap API.", module.body(self.data))

    def test_rejects_extra_and_control_fields(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "draft.json"
            for changed in (dict(self.data, project_secret="private"), dict(self.data, title="bad\x00title")):
                path.write_text(json.dumps(changed), encoding="utf-8")
                with self.assertRaises(ValueError):
                    module.load_draft(path)

    @patch.object(module, "run_gh")
    def test_duplicate_does_not_create(self, gh):
        gh.return_value = json.dumps([{"title": "SHARED  PLACEMENT SNAPPING", "url": "https://example.test/1"}])
        self.assertEqual(module.submit(self.data), "Existing issue: https://example.test/1")
        gh.assert_called_once()

    @patch.object(module, "run_gh")
    def test_create_uses_minimal_body(self, gh):
        gh.side_effect = ["[]", "https://example.test/2"]
        self.assertEqual(module.submit(self.data), "Created issue: https://example.test/2")
        self.assertEqual(gh.call_args.args[-1], module.body(self.data))


if __name__ == "__main__":
    unittest.main()
