#pragma once
#include "Workspace.h"

namespace rts::editor {
Json runProjectPort(const std::string& engineDir, const std::string& operation, const Json& params);
void registerPortOps(Workspace& ws);
}
