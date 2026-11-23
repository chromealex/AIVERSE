import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib
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
            (root / "docs/evidence").mkdir(parents=True)
            log = root / "docs/evidence/test.log.txt"
            log.write_text("fixture")
            report = root / "docs/evidence/test.json"
            report.write_text(json.dumps(dict(sha="a"*40, argv=["test"], exit_code=0, log="docs/evidence/test.log.txt", sha256=hashlib.sha256(log.read_bytes()).hexdigest())))
            data["test_reports"] = ["docs/evidence/test.json"]
            data["integrations"] = dict(skills=["sample"], mcp_tools=["sample_get"], studio_panels=["sample"], studio_methods=["studio.sample"])
            fixtures = {
                "skills/catalog.json": '{"skills":[{"id":"sample","path":"skills/workflow/sample/SKILL.md"}]}',
                "skills/workflow/sample/SKILL.md": "name: sample\n",
                "apps/studio-web/src/nestedDock.ts": "export const STUDIO_PANELS = ['sample'] as const;",
                "apps/studio-web/src/panels/StudioPanelRenderer.tsx": "switch (panel) { case 'sample': return null; }",
                "apps/studio-web/src/types.ts": "export interface StudioParams {\n  'studio.sample': {};\n}\n"
            }
            for name, content in fixtures.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            self.assertEqual(gate.validate(data, root, True), [])
            log.write_text("tampered")
            self.assertTrue(gate.validate(data, root, True))
            log.write_text("fixture")
            data["integrations"]["studio_panels"] = ["missing"]
            self.assertTrue(gate.validate(data, root, True))
            data["integrations"]["studio_panels"] = ["sample"]
            data["integrations"]["skills"] = ["missing"]
            self.assertTrue(gate.validate(data, root, True))
            data["integrations"]["skills"] = ["sample"]
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
        self.assertEqual(covered_paths(changed, {"manifest": {"changed_paths": ["engine/a.cpp", "apps/studio-web/a.ts"], "traits": {"runtime": True}}}), [])
        self.assertTrue(covered_paths(changed, {"manifest": {"changed_paths": ["engine/*"]}}))
    def test_bad_shape(self):
        self.assertTrue(gate.validate([]))
        self.assertTrue(gate.validate({"schema": True}))

if __name__ == "__main__":
    unittest.main()
