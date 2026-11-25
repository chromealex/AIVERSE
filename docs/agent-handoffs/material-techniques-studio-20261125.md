# Material techniques in Studio Models

SHA efb765cb1254aef37801c09e25a5641b36157f47. The inspector in apps/studio-web/src/panels/MaterialTechniquePanel.tsx uses the same native Workspace operations as MCP. ToolPanel.tsx mounts the editor with project-scoped state. Drafts can be discarded; undo/redo of a saved change compare the current rules against the previous exact value before persisting through the native writer.

The project under demos/material-techniques shows a named glTF material, its authored sidecar, and a camera distance control. CMakeLists.txt registers tests/render/material_technique_frame.cpp, which checks the dominant pixel channel from three real Metal render targets for the near, far and inspection rules, when AIVERSE_MATERIAL_TECHNIQUE_CAPTURE designates an existing output directory.

Test commands already run: npm run typecheck in apps/studio-web (exit 0); Vite 7.3.8 production build to the KINGSTON stage directory (exit 0 with the existing bundle size warning); clang++ -std=c++20 -I. -fsyntax-only tests/render/material_technique_frame.cpp (exit 0). The demo is 3D content, not a screenshot. Headless MCP, Metal capture, browser QA, web packaging and merged-tree gate still require runtime evidence.
