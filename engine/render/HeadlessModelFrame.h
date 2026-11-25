#pragma once
#include "Model.h"
namespace aiverse::render {
bool headlessModelBackendAvailable(const std::string& backend);
std::unique_ptr<Model> loadHeadlessModel(const std::string& utf8Path);
// Returns top-down RGBA8. Angles are in radians; the bind pose is framed automatically.
std::vector<uint8_t> renderHeadlessModelFrame(const Model& model,const std::string& backend,
                                              int width,int height,float yaw,float pitch);
}
