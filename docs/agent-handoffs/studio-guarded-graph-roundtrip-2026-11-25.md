# Native Studio graph saves across WebView

Source f54a8298f preserves the native serialization of the original graph across the browser bridge. The regression uses the mutable JSON array accessor in 647255066a3e10cb97b1f40bd303d62154c170f2. The native owners retain their original comparison and write logic. Clients may continue to send an original document object. Studio panels return the exact original string supplied by Studio and discard it when loading another document.

At 911e3ceba the production Shader Graph panel selected the tint node in examples/shader_graph/material_tint.json and changed R from 0.2 to 0.375. Save incorrectly reported a change to the on-disk graph. The existing reader preserves the source spelling of numbers. The native WebView object bridge normalizes numbers when the original is sent back as an object. The standalone guarded write test did not exercise that transport.

On 647255066 an actual macOS Studio window opened the same three-node graph in a disposable project, selected tint, edited its R field, validated and saved it. The file contained 0.375 with the original topology. A separate writer then set that field to 0.625. A second panel save using the old original was rejected, and the file still contained 0.625. The conflict capture is docs/evidence/studio-graph-647255066/stale-edit.png.

The same Mac Studio bridge created, read, changed and saved assets using fx_graph_set, texture_graph_set and animation_graph_set. Each originalJson came from the native get response. All three refused a second write with the stale original. A fresh read in each case returned the changed asset. The same session verified two regions through authenticated MCP, profiler start/status/stop and exact single stepping. The production web build and all eight clean-tree suites passed. The reports prefixed feature-gate-647255066 identify the tested commit and log digests.

The earlier full Mac quick on 911e3ceba stopped during the link of aiverse_rts_authored_pose_test with Apple clang reporting a signal 11 from the linker. CTest did not start in that invocation.

The reduced-parallelism retry successfully linked aiverse_rts_authored_pose_test. The new StudioSession fixture then stopped at its first baseline assertion: its graph omitted the required version field and was refused on load. The real versioned project and three additional owner routes described above had passed. Correct the fixture to use schema version 1 and check the get response before inspecting its original.
