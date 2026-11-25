# Building Graph / Caverns on the integrated Studio and simulation

The integration includes all four source PRs and the other changes carried by their integrator branch. On d5608fb7f the merged Building Studio runner completed all nine native CTests, including studio_session, and its web and Python checks. The separate library_building_runtime target was rebuilt on that revision and passed. The ten feature records were refreshed in b1bb899495038c32e67a486ba337ec025342046b. Commit 7ce2a7e90be0a626731994c07e4e357302e2d72f records the standalone recurring budget consumer and both headless Traffic tests, including checksum parity after saving, reopening and advancing 80 ticks.

## Native Studio acceptance

The packaged macOS Studio used an isolated copy of examples/projectiles at /private/tmp/aiverse-graph-ui-47ZdeL/Graph-QA. The executable was built from f2fdc7499. The subsequent integration changes through 3e2a609f0 affect tests and records only. A second application instance stalled while Workspace::log opened editor.log. Closing the second test process exposed the original instance with the project loaded. The following checks were performed in the original native window.

| Action | Observed |
| --- | --- |
| Caverns → Read current map on maps/demo.json | Revision 1. The panel reported no authored snapshot or selected planetary region, matching this map. |
| Building Generator → Preview 3D | A generated 3D model appeared in the panel with the original Scene still visible. |
| Evaluate 3 seeds → Graphs | Three candidates. The selected seed 482733 showed one room, four exterior entrances, five visible nodes and one connected group. The geometry panel listed 39 solids and five sockets. |
| Generate GLB | Native Studio reported the asset saved. The binary contains 468 triangles, 16 materials and 64 embedded images. On disk scifi-custom-482733.glb was 16848632 bytes, SHA-256 abb13efee7f37696cd8ccccc50076e9d2d8d565c476e488ea8f48f2bc2fabb8f. The editable recipe and asset metadata were present, with GUID 9ff4b742f07249f89e86f6071b075745. |
| Materials → hull · metal | Native previews for base color, metallic/roughness, normal and AO at 256 × 256. |

After the map was edited via native MCP and saved, a fresh Studio process opened the same project. Read current map showed archive version 1, revision 3, exactly two stamps, carve #2 at 400 cm and fill #3 at 200 cm, and zero entrances.

The screen capture of the exported asset was observed in-session. The project and generated assets are local QA fixtures. These checks do not establish live pathfinding, visual publication, or GPU performance budgets.

## Regression found by the merged-tree gate

The Mac quick build of bd85676b6 failed compiling tests/vfx_graph_ops_test.cpp. Commit 98b1eddcce5d6040c5700e8f819a1efbf9556c86 restored the shared assertion calls. Commit a8cf3f332 separated the shader precondition and preview ambiguity fixture names. The resulting CTest then failed its SDF no-publication assertion because an earlier fixture had deliberately published a different graph in the same directory. Commit 3e2a609f07c6c18b934f08e1a1bf53a498b18b93 fixes isolation with a separate Workspace and project under the test-owned temporary directory. The absent-file assertions remain intact. The rebuilt and linked standalone fixture passed graph CRUD, preview, validation, variants, baking and path safety.

The Windows cross-build produced x86-64 PE executables. Windows execution and the platform CI matrix require actual runner results. The local Linux Docker probe stayed in Created with PID zero even for uname with no mounts or networking and was removed. Main promotion still requires a successful strict feature audit and a complete Mac quick run on its exact candidate tree.
