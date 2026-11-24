import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib
import sys
import subprocess
import shutil
import io
from contextlib import redirect_stdout, redirect_stderr
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/agents"))
from evidence import validate_report, checkout_errors
from audit_compliance import covered_paths
from feature_registry import regressions, matrix, load_registry, owner_readiness
from feature_policy import dependency_report, roadmap_report, api_report, ownership_report
from feature_automation import (impact_report, release_scope, schedule_tasks, skill_similarity,
                                architecture_report, skill_freshness_candidates, conflict_plan,
                                task_context, scaffold_gap, export_assignments, feature_history,
                                coverage_history, discover_branches)
from feature_api import compare as compare_api
from new_feature import scaffold
from legacy_inventory import inventory
from capability_inventory import capability_id, source_operations
from capability_coverage import LEDGER, SURFACES, read_coverage, apply_coverage
from graph_inventory import inventory as graph_inventory

spec = importlib.util.spec_from_file_location("feature_gate", Path(__file__).resolve().parents[1] / "tools/agents/check_feature.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

mcp_spec = importlib.util.spec_from_file_location("mcp_audit", Path(__file__).resolve().parents[1] / "tools/mcp/audit_registry.py")
mcp_audit = importlib.util.module_from_spec(mcp_spec)
mcp_spec.loader.exec_module(mcp_audit)


class McpAuditTest(unittest.TestCase):
    def run_audit(self, responses, flags=(), returncode=0):
        stdout, stderr = io.StringIO(), io.StringIO()
        process = subprocess.CompletedProcess(
            ["fixture"], returncode,
            stdout="\n".join(json.dumps(r) for r in responses), stderr="")
        with patch.object(sys, "argv", ["audit_registry.py", "fixture", *flags]), \
             patch.object(mcp_audit.subprocess, "run", return_value=process), \
             redirect_stdout(stdout), redirect_stderr(stderr):
            result = mcp_audit.main()
        return result, stdout.getvalue(), stderr.getvalue()

    def responses(self):
        names = sorted({name for group in mcp_audit.REQUIRED.values() for name in group})
        payload = {"count": len(names), "tools": [
            {"name": name, "guiOnly": False} for name in names]}
        return [
            {"id": 1, "result": {"protocolVersion": "2024-11-05"}},
            {"id": 2, "result": {"tools": [{"name": name} for name in names]}},
            {"id": 3, "result": {"content": [{"type": "text", "text": json.dumps(payload)}]}}
        ]

    def test_protocol_failures(self):
        self.assertEqual(self.run_audit(self.responses())[0], 0)
        self.assertEqual(self.run_audit(self.responses(), returncode=1)[0], 2)
        self.assertEqual(self.run_audit(self.responses()[1:])[0], 2)
        responses = self.responses()
        self.assertEqual(self.run_audit(responses + [responses[1]])[0], 2)
        responses[1]["result"]["nextCursor"] = "more"
        self.assertEqual(self.run_audit(responses)[0], 2)
        responses = self.responses()
        responses[1]["result"]["tools"].append(responses[1]["result"]["tools"][0])
        self.assertEqual(self.run_audit(responses)[0], 2)
        responses = self.responses()
        responses[2]["result"]["isError"] = True
        self.assertEqual(self.run_audit(responses)[0], 2)

    def test_feature_registration_gaps(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for ident, names in (("existing", ["mcp_registry"]), ("missing", ["unknown_fixture_tool"])):
                (root / (ident + ".json")).write_text(json.dumps({
                    "id": ident, "integrations": {"mcp_tools": names},
                    "checks": {"mcp": {"status": "not_run"}}
                }))
            code, output, _ = self.run_audit(self.responses(), ["--feature-dir", folder])
            self.assertEqual(code, 1)
            rows = {row["id"]: row for row in (
                json.loads(line[len("FEATURE "):]) for line in output.splitlines()
                if line.startswith("FEATURE "))}
            self.assertEqual(rows["existing"]["registration"], "registered")
            self.assertEqual(rows["existing"]["runtime_verification"], "not_run")
            self.assertEqual(rows["missing"]["missing"], ["unknown_fixture_tool"])


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
    def test_ready_cli_requires_git_provenance(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            manifest = root / "docs/features/manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("{}")
            for flags, strict in (([], False), (["--ready"], True)):
                with patch.object(sys, "argv", ["check_feature.py", str(manifest), *flags]):
                    with patch.object(gate, "ROOT", root), patch.object(gate, "checkout_errors", return_value=[]):
                        with patch.object(gate, "validate", return_value=[]) as validate:
                            self.assertEqual(gate.main(), 0)
                            validate.assert_called_once_with({}, ready=strict, verify_git=strict)

    def test_git_evidence_staleness(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            def git(*args):
                return subprocess.check_output(["git", *args], cwd=root, text=True).strip()
            git("init", "-q")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            source = root / "source.py"
            source.write_text("version = 1\n")
            git("add", ".")
            git("commit", "-qm", "source")
            sha = git("rev-parse", "HEAD")
            log = root / "docs/evidence/test.log.txt"
            log.parent.mkdir(parents=True)
            log.write_text("fixture")
            report = dict(sha=sha, argv=["test"], exit_code=0,
                          log="docs/evidence/test.log.txt",
                          sha256=hashlib.sha256(log.read_bytes()).hexdigest())
            git("add", ".")
            git("commit", "-qm", "evidence only")
            self.assertEqual(validate_report(report, root, True), [])
            self.assertEqual(checkout_errors(root), [])
            self.assertTrue(checkout_errors(root, sha))
            source.write_text("version = 2\n")
            self.assertTrue(checkout_errors(root))
            self.assertEqual(checkout_errors(root, clean=False), [])
            self.assertTrue(any("clean checkout" in error for error in validate_report(report, root, True)))
            git("add", ".")
            git("commit", "-qm", "source changed")
            self.assertEqual(validate_report(report, root, False), [])
            self.assertTrue(any("tested tree differs" in error
                                for error in validate_report(report, root, True)))

    def test_owner_readiness(self):
        registry = {"new": {"owner": "render", "checks": {}},
                    "done": {"owner": "world", "checks": {
                        key: {"status": "pass"} for key in gate.DIMENSIONS}}}
        capabilities = {"capabilities": [{"id": "render:shader", "owner": "render"}],
                        "tasks": [{"id": "audit:render:shader", "owner": "render",
                                   "kind": "investigate"}], "errors": []}
        graphs = {"graphs": [{"id": "shader", "owner": "render"}],
                  "tasks": [{"id": "graph-audit:shader", "owner": "render",
                             "kind": "investigate"}], "errors": []}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch("feature_registry.validate", side_effect=lambda data, root:
                       ["fixture error"] if data["owner"] == "render" else []):
                report = owner_readiness(registry, root, capabilities, graphs)
                self.assertEqual(report["counts"], {"manifests": 2, "capabilities": 1,
                                                    "graphs": 1, "tasks": 3})
                render, world = report["owners"]
                self.assertEqual(render["owner"], "render")
                self.assertEqual(render["manifests"][0]["state"], "invalid")
                self.assertEqual(render["tasks"][0]["kind"], "fix_manifest")
                self.assertEqual(world["manifests"][0]["state"], "declared_ready")
                self.assertEqual(world["tasks"], [])
                filtered = owner_readiness(registry, root, capabilities, graphs, "render")
                self.assertEqual(len(filtered["owners"]), 1)
                self.assertEqual(filtered["counts"]["manifests"], 1)
                self.assertTrue(owner_readiness(registry, root, capabilities, graphs,
                                                "absent")["errors"])
                graphs["tasks"].append(dict(graphs["tasks"][0]))
                self.assertTrue(any("duplicate audit task" in e for e in
                                    owner_readiness(registry, root, capabilities, graphs)["errors"]))

    def test_reviewed_capability_coverage(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            ledger = root / LEDGER
            ledger.parent.mkdir(parents=True)
            log = ledger.parent / "audit.log"
            log.write_text("fixture audit")
            report = ledger.parent / "audit.json"
            report.write_text(json.dumps({"sha": "a" * 40, "argv": ["audit"], "exit_code": 0,
                "log": "docs/evidence/audit.log", "sha256": hashlib.sha256(log.read_bytes()).hexdigest()}))
            record = {"capability": "render:shader", "surface": "mcp", "status": "gap",
                      "note": "fixture missing tool", "reviewer": "fixture",
                      "report": "docs/evidence/audit.json"}
            ledger.write_text(json.dumps({"schema": 1, "records": [record]}))
            records, errors = read_coverage(root, {"render:shader"}, verify_git=False)
            self.assertEqual(errors, [])
            row = {"id": "render:shader", "owner": "render", "title": "Shader",
                   "coverage": {surface: "unknown" for surface in SURFACES}}
            investigation = {"id": "audit:render:shader", "capability": row["id"],
                             "owner": "render", "kind": "investigate"}
            tasks = apply_coverage([row], [investigation], records)
            self.assertEqual(row["coverage"]["mcp"], "gap")
            self.assertEqual(tasks[0]["kind"], "implement_gap")
            self.assertEqual(tasks[0]["id"], "implement:render:shader:mcp")
            self.assertNotIn("mcp", tasks[1]["surfaces"])
            self.assertIn("studio", tasks[1]["surfaces"])
            ledger.write_text(json.dumps({"schema": 1, "records": [record, record]}))
            records, errors = read_coverage(root, {"render:shader"}, verify_git=False)
            self.assertFalse(records)
            self.assertTrue(any("duplicate" in error for error in errors))
            ledger.write_text(json.dumps({"schema": 1, "records": [record]}))
            log.write_text("tampered")
            records, errors = read_coverage(root, {"render:shader"}, verify_git=False)
            self.assertFalse(records)
            self.assertTrue(any("digest" in error for error in errors))

    def test_feature_policy_dependencies_and_conflicts(self):
        checks = {key: {"status": "pass"} for key in gate.DIMENSIONS}
        registry = {"base": {"checks": checks}, "child": {"checks": checks}}
        policies = {"base": {"requires": [], "roadmap": {"system": "core", "id": "base"},
                             "api_paths": ["engine/Base.h"]},
                    "child": {"requires": ["base"], "roadmap": None, "api_paths": []}}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch("feature_policy.validate", return_value=[]):
                report = dependency_report(registry, policies, root)
                self.assertEqual(report["order"], ["base", "child"])
                self.assertEqual(report["features"]["child"]["state"], "declared_ready")
                policies["base"]["requires"] = ["child"]
                self.assertTrue(any("cycle" in e for e in dependency_report(registry, policies, root)["errors"]))
                policies["base"]["requires"] = []
                registry["base"] = {"checks": {}}
                report = dependency_report(registry, policies, root)
                self.assertEqual(report["features"]["child"]["state"], "blocked")
                self.assertEqual(report["features"]["child"]["blocked_by"], ["base"])
        roadmap = roadmap_report(registry, policies, [{"id": "core", "roadmap": [{"id": "base", "status": "done"}]}])
        self.assertTrue(any("pending" in e for e in roadmap["errors"]))
        self.assertEqual(roadmap["unmapped"], ["child"])
        self.assertTrue(api_report(policies, {"engine/Base.h"})["errors"])
        self.assertFalse(api_report(policies, {"engine/Base.h", "docs/features/base.json"})["errors"])
        overlap = ownership_report([{"agent": "a", "branch": "one", "paths": ["engine/Base.h"]},
                                    {"agent": "b", "branch": "two", "paths": ["engine/Base.h"]}])
        self.assertEqual(overlap["conflicts"][0]["path"], "engine/Base.h")

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

    def test_graph_inventory(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            row = dict(id="shader", title="Shader", owner="render", kind="authoring", source="engine/Graph.h",
                       skill="shader", mcp_candidates=["graph_set", "unknown_op"], studio_candidates=["graph"])
            files = {
                "docs/agents/graph-inventory.json": json.dumps({"graphs": [row]}),
                "skills/catalog.json": json.dumps({"skills": [{"id": "shader"}]}),
                "engine/Graph.h": "// fixture",
                "apps/editor/core/GraphOps.cpp": 'add("graph_set", "fixture");',
                "apps/studio-web/src/nestedDock.ts": "export const STUDIO_PANELS = ['graph'] as const;"
            }
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            report = graph_inventory(root)
            self.assertFalse(report["errors"])
            self.assertEqual(report["graphs"][0]["verification"], "not_run")
            self.assertEqual(report["graphs"][0]["literal_mcp_matches"], ["graph_set"])
            self.assertEqual(report["graphs"][0]["unmatched_mcp_candidates"], ["unknown_op"])
            self.assertEqual(report["graphs"][0]["studio_matches"], ["graph"])
            self.assertEqual(report["tasks"][0]["kind"], "investigate")

    def test_bad_shape(self):
        self.assertTrue(gate.validate([]))
        self.assertTrue(gate.validate({"schema": True}))

class AutomationTest(unittest.TestCase):
    def fixture(self):
        registry = {"base": {"id":"base", "owner":"core", "changed_paths":["engine/Base.h"]},
                    "child": {"id":"child", "owner":"render", "changed_paths":["engine/Child.h"]}}
        policies = {"base":{"requires":[], "api_paths":["engine/Base.h"]},
                    "child":{"requires":["base"], "api_paths":[]}}
        deps = {"order":["base","child"], "features":{
            "base":{"requires":[], "blocked_by":[], "state":"pending"},
            "child":{"requires":["base"], "blocked_by":["base"], "state":"blocked"}}}
        return registry, policies, deps

    def test_impact_release_and_schedule(self):
        registry, policies, deps = self.fixture()
        impact = impact_report(registry, policies, ["engine/Base.h","unknown.h"])
        self.assertEqual(impact["direct"], {"base":["engine/Base.h"]})
        self.assertEqual(impact["transitive"], ["child"])
        self.assertEqual(impact["unmapped_paths"], ["unknown.h"])
        selection = release_scope({"releases":{"next":["child"]}}, "next", deps)
        self.assertEqual(selection["features"], ["base","child"])
        self.assertEqual(selection["implicit_prerequisites"], ["base"])
        with self.assertRaises(ValueError):
            release_scope({"releases":{"next":["missing"]}}, "next", deps)
        readiness = {"owners":[
            {"owner":"core","tasks":[{"id":"base-task","feature":"base","kind":"fix_manifest"}]},
            {"owner":"render","tasks":[{"id":"child-task","feature":"child","kind":"investigate"}]}]}
        agents = [{"id":"a","owners":["core","render"],"capacity":2}]
        rows = schedule_tasks(readiness, deps, agents, set(), registry)
        self.assertEqual(rows[0]["agent"], "a")
        self.assertEqual(rows[1]["state"], "blocked")
        self.assertIn("base", rows[1]["reason"])
        self.assertEqual(schedule_tasks(readiness, deps, agents, {"engine/Base.h"}, registry)[0]["state"], "blocked")

    def test_skill_and_architecture_review(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for rel in ("skills/a/SKILL.md", "skills/b/SKILL.md"):
                path = root / rel
                path.parent.mkdir(parents=True)
                path.write_text("---\nname: fixture\n---\nshared deterministic policy and owner validation\n")
            skills = [{"id":"a","domain":"core","path":"skills/a/SKILL.md","source":"engine/Base.h"},
                      {"id":"b","domain":"core","path":"skills/b/SKILL.md"}]
            self.assertEqual(skill_similarity(root, skills)[0]["skills"], ["a","b"])
            self.assertEqual(skill_freshness_candidates(skills, ["engine/Base.h"])[0]["skill"], "a")
            src = root / "engine/sim/Bad.cpp"
            src.parent.mkdir(parents=True)
            src.write_text('#include "../../modules/fps/Bad.h"\nif (isFps()) {}\nint v=rand();\n')
            findings = architecture_report(root, ["engine/sim/Bad.cpp"], [])["findings"]
            self.assertEqual(len(findings), 3)
            self.assertEqual(architecture_report(root, ["engine/sim/Bad.cpp"], ["engine/sim/Bad.cpp"])["findings"], [])
            conflict = {"path":"engine/Base.h","claims":[{"branch":"a"},{"branch":"b"}]}
            self.assertEqual(conflict_plan([conflict], self.fixture()[0])[0]["action"], "coordinate_with_single_owner")

    def test_context_scaffolding_and_assignment_export(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for rel,data in {"docs/devlog/systems.json":{"systems":[]},
                             "skills/catalog.json":{"skills":[]}}.items():
                path = root / rel
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(json.dumps(data))
            registry, _, deps = self.fixture()
            task = {"id":"gap:mcp","kind":"implement_gap","feature":"base","surface":"mcp"}
            report = {"sha":"a"*40,"readiness":{"owners":[{"owner":"core","tasks":[task]}]},
                      "policy":{"dependencies":deps},
                      "automation":{"schedule":[{"task":"gap:mcp","agent":"a","state":"proposed"}]}}
            ctx = task_context(root, report, registry, "gap:mcp")
            self.assertEqual(ctx["paths"], ["engine/Base.h"])
            self.assertIn("AGENTS.md", ctx["missing_references"])
            draft = scaffold_gap(root, report, registry, "gap:mcp", root/"drafts")
            self.assertTrue((draft/"CapabilityOps.h.in").is_file())
            with self.assertRaises(FileExistsError):
                scaffold_gap(root, report, registry, "gap:mcp", root/"drafts")
            result = export_assignments(root, report, registry, root/"assignments")
            self.assertEqual(result["agents"], 1)
            self.assertEqual(json.loads(Path(result["assignments"][0]).read_text())["state"], "proposed")

    def test_git_history_and_branch_discovery(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            def git(*args):
                return subprocess.check_output(["git",*args],cwd=root,text=True).strip()
            git("init","-q","-b","main")
            git("config","user.name","Fixture")
            git("config","user.email","fixture@example.invalid")
            manifest = root/"docs/features/base.json"
            manifest.parent.mkdir(parents=True)
            def write(status):
                manifest.write_text(json.dumps({"checks":{"tests":{"status":status}},"integrations":{}}))
                git("add",".")
                git("commit","-qm",status)
            write("pass")
            git("switch","-qc","feature/fixture")
            write("not_run")
            history = feature_history(root,"base")
            self.assertEqual(history[0]["regressions"], ["tests"])
            self.assertEqual(discover_branches(root,"main"), ["refs/heads/feature/fixture"])

    @unittest.skipUnless(shutil.which("clang++"), "clang++ unavailable")
    def test_compiler_api_comparison(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name, arg in (("old","int"),("new","float")):
                tree = root/name
                tree.mkdir()
                (tree/"Public.h").write_text("namespace demo { struct API { int version() const; private: int secret("+arg+"); }; int work("+arg+"); }")
            report = compare_api(root/"old",root/"new",["Public.h"],"clang++",[])
            self.assertTrue(report["potential_breaking"])
            self.assertEqual({row[1] for row in report["headers"][0]["removed"]},{"demo::work"})
            self.assertEqual({row[1] for row in report["headers"][0]["added"]},{"demo::work"})


if __name__ == "__main__":
    unittest.main()
