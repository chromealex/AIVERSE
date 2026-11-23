import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("feature_gate", Path(__file__).resolve().parents[1] / "tools/agents/check_feature.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class GateTest(unittest.TestCase):
    def test_evidence_and_readiness(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "proof.txt").write_text("fixture")
            data = dict(schema=1, id="sample", owner="test", checks={
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
    def test_bad_shape(self):
        self.assertTrue(gate.validate([]))
        self.assertTrue(gate.validate({"schema": True}))

if __name__ == "__main__":
    unittest.main()
