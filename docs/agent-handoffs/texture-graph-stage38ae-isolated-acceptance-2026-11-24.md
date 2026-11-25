# Stage 38AE — isolated acceptance rerun and native gate (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Exact tested source: `170937b242b44121e3b53dff202ed568741c9fba`
Draft PR: https://github.com/chromealex/AIVERSE/pull/133

Stage 38AE acceptance checkpoint: four isolated exact-blob CPU fixtures at 170937b242b44121e3b53dff202ed568741c9fba (texture_graph_image_pins, vulkan_scope_timing, texture_graph_liveness, texture_graph_output_reuse) passed GCC 14.2, Clang 17 and Clang ASan/UBSan, all with -std=c++20 -Wall -Wextra -Werror. All eight source Git blob hashes matched. Full checkout blocked by GitHub DNS; no /dev/dri. GitHub Actions list for exact HEAD returned zero runs; draft PR #133 remains open. Integrated 30-test suite, actual Metal/Vulkan, Client/Editor/Studio/MCP and Mac merged-tree gate NOT RUN.

## Verified source blobs

| Path | Git blob SHA |
|---|---|
| engine/render/graph/TextureGraphImagePins.h | 535cf9189549eab17665cf0f6b4bae013ead204b |
| tests/render/texture_graph_image_pins.cpp | edb65d0959ab0db73275a2a6bbce8d1e673667da |
| engine/gfx/vulkan/VkScopeTiming.h | 1adac2e8d8193766bc66ad43684938b61fd739da |
| tests/render/vulkan_scope_timing.cpp | 7248c871c2ff3862388626423d85e0781748ded9 |
| engine/render/graph/TextureGraphLiveness.h | 313d7640cb2c1fccc65745d5569ccf1d80dc1995 |
| engine/render/graph/TextureGraphOutputReuse.h | ad74001fdef4c244f889e2ed3ea9e4b9e530e663 |
| tests/render/texture_graph_output_reuse.cpp | 847fe74f0ceb8a9be1ad36aca99301414143c07c |
| tests/render/texture_graph_liveness.cpp | a5b79548023e381e3d582318d337f4ac4878c6c4 |

Fetched exact content through GitHub, copied to isolated /tmp/aiv38check and verified every file with git hash-object before compiling. Executed for each of the four fixtures:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/$t.cpp -o "$t" && "./$t"
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/$t.cpp -o "$t" && "./$t"
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I. tests/render/$t.cpp -o "$t-san" && "./$t-san"
```

12 compile/run pairs passed. Output:
- PASS immutable material-copy pins, partial replacement, external pin, last-reader release
- PASS scope pairing, bounds, abandonment, timestamp wrap and units
- PASS: shared dependencies, duplicate edges, pinned outputs, pruning, chain lifetime, invalid plans
- PASS output closure, independent branch, configuration, cold cache, no-op, key limit

These are isolated CPU policy tests, not the clean full checkout provenance required for manifest test_reports; status remains not_run for integrated verification. Current environment Linux x86_64, GCC 14.2.0, Clang 17.0.0, CMake 3.31.6; git ls-remote https://github.com/chromealex/AIVERSE.git HEAD failed with DNS resolution error. No CI run on exact source HEAD.

## Next: native acceptance

Use a separate exact checkout on Mac and external-volume build directory. No default policy changes or main merge. Configure with tests, editor, tools, HTML5 Studio and GPU fixture enabled, then run tools/texture-graph-stage38-acceptance.py --backend metal. Collect full configure/build/test output and exact SHA. The runner covers 30 selected CTest tests, including the strict real Metal fixture; separate material_batches --unit, live Studio/MCP and merged-tree quick gate remain outstanding.
