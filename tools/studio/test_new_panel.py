import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from new_panel import generate

MODEL = """export const STUDIO_PANELS = ['hierarchy','scene'] as const;
export type StudioPanel = typeof STUDIO_PANELS[number];
export const STUDIO_PANEL_TITLES: Record<StudioPanel,string> = {
  hierarchy: 'Hierarchy', scene: 'Scene'
};
export const STUDIO_PANEL_HOME: Record<StudioPanel,string> = {
  hierarchy: 'dock-left', scene: 'dock-center'
};
"""
RENDERER = """import type { ReactNode } from 'react';
export interface StudioPanelContext { snapshot: unknown }
export function renderStudioPanel(panel: string, ctx: StudioPanelContext): ReactNode {
  const {snapshot}=ctx;
  switch(panel) {
      // AI_PANEL_CASES: insert here
      default: return null;
  }
}
"""

class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        src = self.root / 'apps/studio-web/src'
        (src / 'panels').mkdir(parents=True)
        (src / 'nestedDock.ts').write_text(MODEL, encoding='utf-8')
        (src / 'panels/StudioPanelRenderer.tsx').write_text(RENDERER, encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def test_generator_creates_registered_component_and_styles(self):
        paths = generate(self.root, 'lighting_debug', 'Lighting & debug', 'right')
        self.assertEqual(len(paths), 4)
        src = self.root / 'apps/studio-web/src'
        model = (src / 'nestedDock.ts').read_text()
        renderer = (src / 'panels/StudioPanelRenderer.tsx').read_text()
        component = (src / 'panels/generated/LightingDebugPanel.tsx').read_text()
        self.assertIn("'lighting_debug'", model)
        self.assertIn('lighting_debug: "dock-right"', model)
        self.assertIn('Lighting & debug', model)
        self.assertIn("case 'lighting_debug'", renderer)
        self.assertIn('LightingDebugPanel', component)
        self.assertTrue((src / 'panels/generated/LightingDebugPanel.css').exists())
        self.assertNotIn('imgui', component.lower())
        self.assertIn("from '../../ui'", component)
        self.assertIn('StudioPanel', component)
        self.assertIn('StudioEmptyState', component)
        self.assertNotIn('PanelTitle', component)
        self.assertNotIn('#', (src / 'panels/generated/LightingDebugPanel.css').read_text())

    def test_refuses_invalid_name_and_preserves_files(self):
        src = self.root / 'apps/studio-web/src/nestedDock.ts'
        for value in ('a', '../outside', 'HasSpace', 'x<img>'):
            with self.assertRaises(ValueError):
                generate(self.root, value, 'Title', 'center')
        self.assertEqual(src.read_text(), MODEL)

    def test_duplicate_is_rejected_before_any_mutation(self):
        generate(self.root, 'my_panel', 'My Panel', 'left')
        src = self.root / 'apps/studio-web/src/nestedDock.ts'
        before = src.read_text()
        with self.assertRaises(ValueError):
            generate(self.root, 'my_panel', 'Duplicate', 'right')
        self.assertEqual(src.read_text(), before)

    def test_dry_run_does_not_modify_tree(self):
        paths = generate(self.root, 'debug_panel', 'Debug Panel', 'bottom', dry_run=True)
        self.assertEqual(len(paths), 4)
        self.assertFalse((self.root / 'apps/studio-web/src/panels/generated').exists())
        self.assertEqual((self.root / 'apps/studio-web/src/nestedDock.ts').read_text(), MODEL)

    def test_multiple_panels_preserve_insertion_marker(self):
        generate(self.root, 'foo_bar', 'Foo Bar', 'center')
        generate(self.root, 'baz', 'Baz', 'bottom')
        content = (self.root / 'apps/studio-web/src/panels/StudioPanelRenderer.tsx').read_text()
        self.assertIn('FooBarPanel', content)
        self.assertIn('BazPanel', content)
        self.assertEqual(content.count('AI_PANEL_CASES'), 1)

    def test_escapes_title_in_ts_string_literal(self):
        generate(self.root, 'quote_panel', 'My "Quoted" Panel', 'right')
        generated = (self.root / 'apps/studio-web/src/panels/generated/QuotePanelPanel.tsx').read_text()
        self.assertIn('My \\"Quoted\\" Panel', generated)

if __name__ == '__main__':
    unittest.main()
