#pragma once
#include "Workspace.h"
#include "engine/render/Model.h"
namespace aiverse::editor {
std::unique_ptr<render::Model> loadAnimationModel(const Workspace&,const std::string& path);
void registerAnimationOps(Workspace&);
}
