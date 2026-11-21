#pragma once
// Built-in procedural RTS shapes ("view": {"shape": "worker"}) as CPU models, shared by the
// game view and the Studio Scene. Units are ~1 unit tall facing +X; structures sit on a 1x1
// footprint (scale by the footprint). Team-coloured parts use TeamMode::Tint.
#include "engine/render/Model.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::game {
std::vector<std::pair<std::string, std::unique_ptr<render::Model>>> buildRtsShapeModels();
}
