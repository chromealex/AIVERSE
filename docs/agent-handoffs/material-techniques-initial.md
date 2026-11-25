# Material techniques

Implementation commit 27fb7bc68a54cf1a1d1f6dfa02c4f67611f95467, branch codex/material-techniques-20261125, based on BSP integration branch 54ca01d1b.

Model.h and MaterialTechnique.h store ordered model-owned rules. MaterialTechniqueData.h resolves unique material names and validates authored conditions. Model.cpp reads the existing .batching.json sidecar through both normal and converted asset paths. Renderer3DModels.cpp evaluates distance and capabilities for each submitted mesh node; Renderer3D.h supplies the scheme for the viewport. ModelMerge.cpp guards authored material identity. MaterialOps.cpp adds get/set, validates the proposed sidecar through loadModel before publishing, and preserves unrelated settings. StudioTools.h exposes both ops through the Models panel. docs/models.md and the aiverse-look skill describe the format.

CPU verification used an isolated CMake tree on KINGSTON. The existing model_batching_policy CTest covers an OBJ/MTL import, absent rules, name resolution, distance boundary, scheme and GPU capability fallback, malformed rules, and existing batching regressions. The executable returned PASS. See docs/evidence/material-techniques-selection-20261125.json. Renderer3DModels.cpp and MaterialOps.cpp passed clang++ -std=c++20 -I. -fsyntax-only. No rendered pixels were tested by these checks.

The headless MCP binary build and additional native acceptance work are ongoing. Pending: real material pixel capture, Studio authoring/undo validation, web build/demo and merged-tree Mac quick gate. No main integration.
