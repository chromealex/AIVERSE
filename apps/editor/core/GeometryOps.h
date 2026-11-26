#pragma once
#include <string>

namespace aiverse::editor {
class Workspace;
std::string configuredGeometryScene(const Workspace& ws);
void registerGeometryOps(Workspace& ws);
}
