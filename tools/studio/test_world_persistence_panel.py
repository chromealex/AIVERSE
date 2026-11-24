"""Source contract for World Persistence; native runtime acceptance is separate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "apps/studio-web/src"

class WorldPersistencePanelTests(unittest.TestCase):
    def test_registered_and_native_guarded(self):
        panel = (SRC / "panels/WorldPersistencePanel.tsx").read_text()
        dock = (SRC / "nestedDock.ts").read_text()
        renderer = (SRC / "panels/StudioPanelRenderer.tsx").read_text()
        native = (ROOT / "apps/studio-native/StudioSession.cpp").read_text()
        tools = (ROOT / "apps/studio-native/StudioTools.h").read_text()
        self.assertIn("'world_persistence'", dock)
        self.assertIn("case 'world_persistence'", renderer)
        self.assertIn("nativeConnected()", panel)
        self.assertIn("required.every", panel)
        self.assertIn("expectedProject", panel)
        self.assertIn('params.getStr("expectedProject")!=ws_.project.dir', native)
        for name in ("list", "create", "status", "page", "checkpoint", "backups",
                     "retention_preview", "retention_apply", "fork"):
            self.assertIn("world_save_" + name, tools)
            self.assertIn("world_save_" + name, panel)

    def test_background_job_contract(self):
        panel = (SRC / "panels/WorldPersistencePanel.tsx").read_text()
        for op in ("start", "status", "cancel", "release"):
            self.assertIn("world_job_" + op, panel)
        self.assertIn("cancelAllowed", panel)
        self.assertIn("checkpointTotal", panel)
        self.assertIn("Existing job from", panel)

    def test_confirmation_and_truthful_diagnostics(self):
        panel = (SRC / "panels/WorldPersistencePanel.tsx").read_text()
        self.assertIn("expectedGenerations:confirmed.plan.removeGenerations", panel)
        self.assertIn("Confirm deletion of listed generations", panel)
        self.assertIn("resetRequired", panel)
        self.assertIn("Not collision residency", panel)
        self.assertIn("No percentage is available", panel)
        self.assertIn("StudioPanel", panel)
        self.assertIn("StudioProgress", panel)
        self.assertNotIn("localStorage", panel)

if __name__ == "__main__":
    unittest.main()
