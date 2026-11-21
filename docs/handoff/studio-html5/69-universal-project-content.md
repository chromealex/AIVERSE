# AIVERSE Studio — Stage 69: content directory in all newly created projects

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 6400aaf4caa0e0ea28bd3d55aca13be1aa45b733
Stage commit: see Git history.

## Purpose
Make project/content a universal new-project layout convention, not only an on-open HTML5 Studio migration. All project entry points (legacy ImGui editor, GameWizard, Workspace/MCP create_project, template-based projects) share apps/editor/core/Projects.cpp::createProject.

## Implemented
- After copying template files or writing the minimal empty project, create a real lower-case `content/` directory in the new project root. Existing files inside template-provided `content/` are preserved; directory creation is idempotent.
- Reject `content` symlinks and existing non-directory entries (e.g., template file named content) instead of following a redirect. Propagate a readable creation error to callers.
- Existing project manifests, maps, rules, authored resources and asset reference paths are not rewritten.
- StudioSession continues to provision content/ for older projects upon opening, and Studio Assets continues showing ONLY nested regular files in that folder.
- Added static source-integration checks to tools/studio Python unit discovery. Native C++ filesystem behavior is tested by the separate studio_content_directory test introduced in Stage 67.

## Changed files
apps/editor/core/Projects.cpp
apps/editor/core/Projects.h
docs/studio-ui-agent-guide.md
apps/studio-web/AGENTS.md
tools/studio/test_content_layout.py
docs/handoff/studio-html5/69-universal-project-content.md

## Tests actually performed
Integration guard tests authored, not yet run in the full repository. Full CMake/CTest, frontend npm tests, and WKWebView manual launch NOT RUN in this connector-only environment. GitHub CI in queue at last check; C++ compiler success for generic Projects.cpp not yet confirmed.

## Compatibility and constraints
- Newly created projects always receive content/. Existing projects do NOT undergo destructive migration; content/ is provisioned when opened in Studio.
- The generic Workspace `asset_list` operation is unchanged for other tools/agents. Studio's `studio.assets` is intentionally content-scoped.
- Resources outside content remain untouched; manually move/copy them and update references when appropriate. Engine runtime asset lookup paths have NOT been changed by this stage.
- The Stage 69 patch is in shared Projects.cpp and may be affected by other agents merging ProjectTemplates; resolve carefully without deleting their changes.

## Next
Pull, run Python tests and native Mac verify script. Create a new project through both GameWizard and Studio client, check content directory exists beside game.json. Add samples in content/Models and content/Textures, press Assets Reload, validate counts/folders and absence of game.json. Complete a new handoff with actual results; do not merge into main without QA.
