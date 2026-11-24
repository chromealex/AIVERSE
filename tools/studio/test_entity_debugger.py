"""Source-level regression contract; native/WebView acceptance remains separate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class EntityDebuggerTest(unittest.TestCase):
    def test_native_ownership_and_references(self):
        inspect = (ROOT / "apps/studio-native/StudioRuntimeInspect.h").read_text()
        shared = (ROOT / "apps/editor/core/WorldInspect.cpp").read_text()
        host = (ROOT / "apps/studio-native/StudioHost.cpp").read_text()
        self.assertIn('r.set("$entityRef", Json::string(std::to_string(worldRef(e))))', shared)
        self.assertIn('tryGetConst<world::RegionStableIdentity>', inspect)
        self.assertIn('resourceConst<world::RegionEntityLedger>', inspect)
        self.assertIn('world::worldCounter(tag->id)', inspect)
        self.assertIn('epoch!=currentEpoch', inspect)
        self.assertIn('Runtime inspection project changed', host)
        for prefix in ("sid:", "cell:", "ref:"):
            self.assertIn('starts_with("' + prefix + '")', inspect)
    def test_panel_uses_lossless_refs(self):
        panel = (ROOT / "apps/studio-web/src/panels/generated/RuntimePanel.tsx").read_text()
        self.assertIn("typeof record.$entityRef==='string'", panel)
        self.assertIn("Number.isSafeInteger(record.$entity)", panel)
        self.assertIn("expectedProject:projectPath", panel)
        self.assertIn("key={projectPath}", panel)
        self.assertIn("detail.componentSchemas", panel)
        self.assertIn("Back to previous entity", panel)

if __name__ == "__main__":
    unittest.main()
