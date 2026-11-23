import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/agents"))
from audit_compliance import covered_paths
from feature_registry import regressions, matrix, load_registry
from new_feature import scaffold
from legacy_inventory import inventory
from capability_inventory import capability_id, source_operations

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
    def test_registry_regression(self):
        old = {"a": {"checks": {"tests": {"status": "pass"}}, "integrations": {"skills": ["a"]}, "traits": {"runtime": True}}}
        self.assertEqual(regressions(old, old), [])
        self.assertTrue(regressions(old, {}))
        after = {"a": {"checks": {"tests": {"status": "not_run"}}, "integrations": {"skills": []}, "traits": {"runtime": False}}}
        self.assertEqual(len(regressions(old, after)), 3)
        self.assertEqual(matrix(old)["count"], 1)

    def test_scaffold(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            traits = dict(editable=False, simulation=False, visible=False, runtime=True)
            scaffold(root, "sample", "test", ["engine/a.cpp"], traits, True)
            manifest = load_registry(root)["sample"]
            self.assertEqual(gate.validate(manifest, root), [])
            self.assertTrue(gate.validate(manifest, root, True))
            with self.assertRaises(ValueError):
                scaffold(root, "sample", "test", ["engine/a.cpp"], traits)
            with self.assertRaises(ValueError):
                scaffold(root, "../outside", "test", ["engine/a.cpp"], traits)

    def test_legacy_inventory(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            sources = {
                "docs/devlog/systems.json": {"systems": [{"id": "world", "name": "World", "doc": "docs/world.md"}]},
                "libraries/catalog.json": {"packages": [{"id": "world", "skill": "skills/world/world/SKILL.md"}]},
                "skills/catalog.json": {"skills": [{"id": "world", "path": "skills/world/world/SKILL.md"}]},
                "docs/agents/legacy-integration-map.json": {"systems": {"world": {"skills": ["world"], "mcp": ["world_get"]}}}
            }
            for name, data in sources.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(data))
            result = inventory(root)
            self.assertEqual(result["counts"], {"systems": 1, "libraries": 1, "skills": 1})
            self.assertFalse(result["errors"])
            self.assertTrue(all(row["verification"] == "not_run" for row in result["entries"]))
            self.assertEqual(result["entries"][0]["mcp_candidates"], ["world_get"])

    def test_capability_identity_and_source(self):
        self.assertEqual(capability_id("water", {"id": "flow", "name": "Flow"}), "water:flow")
        self.assertEqual(capability_id("water", {"name": "Flow"}), capability_id("water", {"name": "Flow"}))
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / "apps/editor/core/WaterOps.cpp"
            path.parent.mkdir(parents=True)
            path.write_text('add("water_set", "description");\nws.registerOp({"water_get", "description"});')
            self.assertEqual(set(source_operations(root)), {"water_set", "water_get"})

    def test_bad_shape(self):
        self.assertTrue(gate.validate([]))
        self.assertTrue(gate.validate({"schema": True}))

if __name__ == "__main__":
    unittest.main()
