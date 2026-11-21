#pragma once
// Legacy editor API forwarding to PRISMA. Serialized/MCP operation names stay postfx_*.
#include "PrismaOps.h"
namespace aiverse::editor {
inline void registerPostFxOps(Workspace& ws) { registerPrismaOps(ws); }
inline Json postFxBlock(Workspace& ws, const std::string& scope) { return prismaBlock(ws, scope); }
inline void setPostFxBlock(Workspace& ws, const std::string& scope, const Json& block) {
    setPrismaBlock(ws, scope, block);
}
} // namespace aiverse::editor
