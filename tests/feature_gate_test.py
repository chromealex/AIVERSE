import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/agents"))
from audit_compliance import covered_paths

spec = importlib.util.spec_from_file_location("feature_gate", Path(__file__).resolve().parents[1] / "tools/agents/check_feature.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class GateTest(unittest.TestCase):
    def test_evidence_and_readiness(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "proof.txt").write_text("fixture")
            data = dict(schema=1, id="sample", owner="test", changed_paths=["engine/sample.cpp"], traits=dict(editable=True, simulation=True, visible=True, runtime=True), checks={
                key: dict(status="pass", note="fixture", evidence=["proof.txt"])
                for key in gate.DIMENSIONS})
            self.assertEqual(gate.validate(data, root, True), [])
            data["checks"]["tests"]["status"] = "not_run"
            self.assertEqual(gate.validate(data, root), [])
            self.assertTrue(gate.validate(data, root, True))
            data["checks"]["tests"]["status"] = "not_applicable"
            self.assertTrue(gate.validate(data, root))
            data["checks"]["tests"].update(status="pass", evidence=["../proof.txt"])
            self.assertTrue(gate.validate(data, root, True))
    def test_coverage(self):
        changed = {"engine/a.cpp", "apps/studio-web/a.ts", "docs/a.md"}
        self.assertEqual(len(covered_paths(changed, {})), 2)
        self.assertEqual(covered_paths(changed, {"manifest": {"changed_paths": ["engine/a.cpp", "apps/studio-web/a.ts"]}}), [])
        self.assertTrue(covered_paths(changed, {"manifest": {"changed_paths": ["engine/*"]}}))
    def test_bad_shape(self):
        self.assertTrue(gate.validate([]))
        self.assertTrue(gate.validate({"schema": True}))

if __name__ == "__main__":
    unittest.main()
