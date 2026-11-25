# Stage 143 — Studio and MCP mesh SDF baking

Branch: feature/spectrafx-html5-data-interfaces-20261123
Plan: b96070808640df5393728a541a39431dd11821bd
Implementation: ddbb6b99d2968a7afbb9ff407696a514d3d8c8d8

Shared Workspace fx_graph_edit now accepts bakeSdf for an existing Graph v2 CollisionModule, project-relative content model, dimension 4..16 and optional expectedProjectPath. The operation uses the existing bounded baker and native model loader; model/dependency reads restricted to checked content paths, no symlinks or traversal, total 64 MiB read budget. Model space/bind pose, identity transform in editor. Baked staticSdf object replaces only that property; other collision settings, graph nodes and links are preserved. Standard graph validation returned, no file writes until explicit ordinary graph save/publish. The existing MCP tool advertises the action, no additional tool registration.

HTML5 resource inspector now includes CollisionModule, existing indexed model picker with search/drop/lock, dimension selector, Bake/Rebake, status and rejection of stale graph/project responses. Parent local draft pipeline unchanged; global Undo/Redo not added or verified. No native preview obstacle visualization or high-resolution SDF format expansion. C++ regression source uses tetra OBJ, verifies no disk writes, retained properties and traversal rejection. CMake links existing decoder to vfx ops test. Model loading and all editor/core consumers' link dependencies still need compile verification. No build/CTest/TypeScript/visual/Metal/Vulkan QA run per owner request. No main sync/merge. Next: high-resolution SDF asset/atlas and runtime format expansion; remaining geometry/fluid/lighting/audio/cache/procedural work open.
