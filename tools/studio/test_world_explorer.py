"""Source contract; actual native and WebView acceptance are separate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class WorldExplorerTests(unittest.TestCase):
    def test_host_uses_shared_runtime_diagnostics(self):
        host = (ROOT / "apps/studio-native/StudioHost.cpp").read_text()
        game = (ROOT / "apps/studio-native/StudioGameSurface.cpp").read_text()
        self.assertIn('method == "studio.gameRegions"', host)
        self.assertIn('params.getStr("expectedProject")!=project', host)
        self.assertIn('game_->regions()', host)
        self.assertIn('world::ledgerStatus(*ledger,sim->world())', game)
        self.assertIn('world::simulationLodDiagnosticsJson', game)
        self.assertIn('out.set("durabilityKnown",Json::fromBool(false))', game)
    def test_panel_registered_and_truthful(self):
        src = ROOT / "apps/studio-web/src"
        panel = (src / "panels/generated/WorldExplorerPanel.tsx").read_text()
        self.assertIn("'world_explorer'", (src / "nestedDock.ts").read_text())
        self.assertIn("case 'world_explorer'", (src / "panels/StudioPanelRenderer.tsx").read_text())
        self.assertIn("invoke('studio.gameRegions',{expectedProject:projectPath})", panel)
        self.assertIn("r.cell[1]===y", panel)
        self.assertIn("Missing cells are unknown, not empty.", panel)
        self.assertNotIn("setInterval", panel)
        self.assertNotIn("localStorage", panel)

if __name__ == "__main__":
    unittest.main()
