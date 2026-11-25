# HTML Studio as the sole desktop editor

Retire the native ImGui application and route builds, launchers and packaging to HTML Studio. Preserve shared document, native project and MCP code. Verify CMake targets, release archives, Studio contracts and the integrated Mac build.

## Input and output wiring

The owner requested migration of missing workflows before removal. Keep the old editor, its launch paths and build target until parity is verified.

Implementation db00bf2eb updates apps/studio-web/src/panels/ShaderGraphPanel.tsx, ShaderGraphPanel.css, shaderGraphLayout.ts and tests/shaderGraphEdit.test.mjs. Select a destination node to edit ordered inputs. Graph outputs expose material channels or the single color channel for postfx and particle assets. Native shader_graph_validate and shader_graph_set remain the compilation and persistence owners.

Verification on macOS: npm run test:web passed 198 tests. npm run build completed TypeScript and Vite, producing /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/dist. Native-shell interaction, GPU preview and the merged-tree Mac gate remain pending. The legacy editor is still enabled.

Continue with draft editing and graph preview across domains, then verify the outstanding workflows in docs/studio-html5-parity.md, the MCP live editor link, build targets and packaging.
