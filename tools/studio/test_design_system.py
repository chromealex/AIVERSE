"""Stable contract checks for the agent-facing AIVERSE Studio UI design system."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/"apps/studio-web/src"
COMPONENTS=(
    "StudioPanel", "StudioButton", "StudioIconButton", "StudioSection",
    "StudioField", "StudioInput", "StudioSelect", "StudioTextarea",
    "StudioCheckbox", "StudioBadge", "StudioAlert", "StudioEmptyState",
    "StudioSkeleton", "StudioTabs", "StudioListRow", "StudioProgress"
)
SHARED_CLASSES=(
    "au-panel", "au-panel__header", "au-panel__body", "au-panel__footer",
    "au-button", "au-input", "au-select", "au-textarea", "au-field",
    "au-checkbox", "au-badge", "au-alert", "au-empty", "au-tabs",
    "au-list-row", "au-section", "au-toolbar", "au-card", "au-spinner",
    "au-skeleton", "au-progress"
)

class StudioDesignSystemTests(unittest.TestCase):
    def test_shared_tokens_and_legacy_compatibility(self):
        css=(SRC/"theme/tokens.css").read_text(encoding="utf-8")
        for name in ("--studio-background", "--studio-accent", "--studio-foreground",
                     "--au-panel", "--au-field", "--au-focus", "--au-text-muted",
                     "--au-space-1", "--au-space-8", "--au-radius-md",
                     "--au-font-md", "--au-control-height", "--au-shadow-md"):
            with self.subTest(token=name):
                self.assertIn(name+":",css)
        self.assertIn('data-studio-density="comfortable"',css)

    def test_components_and_css_classes_are_exported(self):
        ts=(SRC/"ui/StudioUI.tsx").read_text(encoding="utf-8")
        css=(SRC/"ui/studio-ui.css").read_text(encoding="utf-8")
        for component in COMPONENTS:
            with self.subTest(component=component):
                signature=r"export\s+function\s+"+re.escape(component)+r"(?:<[^\n]+>)?\s*\("
                self.assertRegex(ts,signature)
        for class_name in SHARED_CLASSES:
            with self.subTest(css=class_name):
                self.assertIn("."+class_name,css)
        self.assertIn("aria-label",ts)
        self.assertIn("aria-selected",ts)
        self.assertIn("prefers-reduced-motion",css)

    def test_loaded_once_and_barrel_exported(self):
        main=(SRC/"main.tsx").read_text(encoding="utf-8")
        self.assertEqual(main.count("import './ui/studio-ui.css';"),1)
        self.assertLess(main.index("tokens.css"),main.index("studio-ui.css"))
        self.assertLess(main.index("studio-ui.css"),main.index("styles.css"))
        self.assertIn("import './ui/editor-shell.css';",main)
        self.assertGreater(main.index("editor-shell.css"),main.index("styles.css"))
        self.assertIn("export * from './StudioUI';",(SRC/"ui/index.ts").read_text())

    def test_native_gpu_surface_selectors_not_modified_by_shared_css(self):
        css=(SRC/"ui/studio-ui.css").read_text(encoding="utf-8")
        self.assertNotIn(".viewport",css)
        self.assertNotIn(".native-composite",css)
        self.assertNotIn("CAMetalLayer",css)
        self.assertNotIn("ImGui",css)

    def test_new_screen_style_rules_are_documented_for_agents(self):
        rules=(ROOT/"apps/studio-web/AGENTS.md").read_text(encoding="utf-8")
        docs=(ROOT/"docs/studio-design-system.md").read_text(encoding="utf-8")
        for token in ("StudioPanel","--au-*","docs/studio-design-system.md"):
            self.assertIn(token,rules)
        self.assertIn("StudioEmptyState",docs)
        self.assertIn("StudioField",docs)

    def test_approved_first_cinematic_design_is_documented(self):
        visual=(ROOT/"docs/studio-visual-direction.md").read_text(encoding="utf-8")
        guide=(ROOT/"docs/studio-ui-agent-guide.md").read_text(encoding="utf-8")
        rules=(ROOT/"apps/studio-web/AGENTS.md").read_text(encoding="utf-8")
        skill=(ROOT/"skills/editor/studio-ui/SKILL.md").read_text(encoding="utf-8")
        self.assertIn("Approved visual baseline: First Cinematic Concept",visual)
        self.assertIn("not a screenshot of the current shipping build",visual.lower())
        self.assertIn("StudioPanel",guide)
        self.assertIn("tools/studio/new_panel.py",guide)
        self.assertIn("first cinematic concept",rules.lower())
        self.assertIn("First Cinematic Concept",skill)
        self.assertIn("docs/studio-visual-direction.md",skill)


    def test_root_agent_discovery_and_skill(self):
        root_agents=(ROOT/"AGENTS.md").read_text(encoding="utf-8")
        skill=(ROOT/"skills/editor/studio-ui/SKILL.md").read_text(encoding="utf-8")
        self.assertIn("docs/studio-design-system.md",root_agents)
        self.assertIn("skills/editor/studio-ui/SKILL.md",root_agents)
        self.assertIn("StudioPanel",skill)
        self.assertIn("npm run typecheck",skill)

if __name__=="__main__":
    unittest.main()
