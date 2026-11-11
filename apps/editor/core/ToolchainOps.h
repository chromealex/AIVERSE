// Toolchain operations of the editor core (MCP agents and the Build window), over aiversebuild
// (tools/aiversebuild/Toolchains.h):
//   toolchain_check    what a target needs on this machine: found (path, version) / missing, sizes
//   toolchain_install  install what a target (or one requirement id) needs into
//                      ~/.aiverse/toolchains, in the background; poll build_status
#pragma once
#include "Workspace.h"

namespace aiverse::editor {

void registerToolchainOps(Workspace& ws);

} // namespace aiverse::editor
