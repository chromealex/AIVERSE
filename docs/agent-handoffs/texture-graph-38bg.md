# Texture Graph Stage 38BG — consolidated acceptance checkpoint (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Source implementation through `2e7e374d7a8230ae572a7faf9ddc5864ed7992e3`. Previous stage: 049e438e07f71704f2aba9fd46346ad646a98c08. No main merge.

## Corrections and verification tooling
- cb707a51: explicit TypeScript Obj typing on clipboard spreads; parenthesize custom numeric parameter literals before shader expression generation; register texture_graph_image_asset in manifest MCP audit.
- tools/texture-graph-stage38-acceptance.py --full adds HDR convolution, Studio session/PBR/material/draft contracts, CPU material batches and both real native GPU frame modes. Strict inventory, verbose evidence, skip rejection and clean tracked SHA checks. 38 CTests expected on selected backend.
- tools/texture-graph-stage38bg-mac.sh copies exact tracked web sources to KINGSTON, builds frontend and runs all npm tests, configures native matching bundle, builds Studio and MCP, invokes strict Metal suite and registry audit. Logs archived on exit; no automatic main merge or GUI launch.

## Real local evidence
Linux container git ls-remote against GitHub failed with DNS resolution, so complete checkout and integrated build unavailable. GitHub connector provided custom compute header blob aa672cc0bcdd3a631eb86120104fd5fd36ec57da; copied locally and verified exact Git blob hash. Wrote isolated harness (not the repository CTest target) exercising scalar/RGB mix, guarded division/sqrt, step, fract, 8 invalid tokens, instruction limit and negative literal emission. Three compile/run pairs passed:
- g++ -std=c++20 -Wall -Wextra -Werror
- clang++ -std=c++20 -Wall -Wextra -Werror
- clang++ same flags plus -fsanitize=address,undefined
All printed PASS. No claim of whole-engine or GPU acceptance. Full frontend/native Studio build, CTest, real Metal/Vulkan, headless MCP and live GUI/MCP remain NOT RUN. Main synchronization and merged-tree Mac quick gate NOT RUN.

## Next
On Mac, use a clean checkout of final handoff HEAD with submodules initialized and run bash tools/texture-graph-stage38bg-mac.sh. Return QA archive printed at exit; fix actual compiler errors/warnings before advancing. Then open printed Studio app and verify graph edit/function asset link/refresh, external PNG, custom expression, PBR view and HDRI with capture evidence; separate Vulkan hardware and merged-tree acceptance.
