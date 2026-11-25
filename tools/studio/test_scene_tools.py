"""Source contracts for the Scene transform tools (stage 98)."""
import pathlib, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
def read(p): return (ROOT / p).read_text(encoding="utf-8")

class SceneToolsContract(unittest.TestCase):
    def test_native_tools(self):
        scene = read("apps/studio-native/StudioScene.cpp")
        for name in ("beginTransformDrag", "updateTransformDrag", "endTransformDrag", "dragPreview", "drawTransformGizmo", "transformCaption"):
            self.assertIn("StudioScene::" + name, scene)
        self.assertIn("TransformTool::Rotate", scene)
        self.assertIn("TransformTool::Scale", scene)
        self.assertEqual(scene.count("proxies_=studioDocumentProxies(doc"), 1, "single cached document-proxy rebuild path")
    def test_host_keys_and_commit(self):
        shell = read("apps/studio-native/mac/Main.mm")
        host = read("apps/studio-native/StudioHost.cpp")
        self.assertIn("case 15: key=K::R", shell)
        self.assertIn("case K::R: setTool(3)", host)
        self.assertIn('request("studio.setProperties"', host, "a drag commits one undo step")
        self.assertIn('kind == "tool"', host)
        self.assertIn('"viewport.action"', host)
        self.assertIn('"viewport.tool"', host)
        for cls in ("StudioShell", "StudioMetalView", "StudioFileDropWebView"):
            self.assertEqual(shell.count("@implementation " + cls + "\n"), 1, cls + " implemented once")
    def test_web_toolbar(self):
        bar = read("apps/studio-web/src/components/SceneToolbar.tsx")
        for key in ("'view'", "'move'", "'rotate'", "'scale'", "snapRotate", "gizmos"):
            self.assertIn(key, bar)
        self.assertIn("SCENE_TOOL_KEYS", read("apps/studio-web/src/components/ViewportPlaceholder.tsx"))

class WebRobustnessContract(unittest.TestCase):
    def test_property_field_hooks_before_early_returns(self):
        src = read("apps/studio-web/src/components/PropertyField.tsx")
        self.assertLess(src.index("const scrub = useRef"), src.index("if (property.type === 'asset')"),
                        "hooks after an early return crash React when a row changes type")
    def test_panels_have_error_boundary(self):
        self.assertIn("<PanelErrorBoundary", read("apps/studio-web/src/App.tsx"))

if __name__ == "__main__":
    unittest.main()
