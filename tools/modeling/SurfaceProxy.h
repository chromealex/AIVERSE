#pragma once
#include "Modeling.h"
namespace aiverse::modeling {
// Host-only directional surface bake. The source must retain CPU geometry and images.
// plane: two opposing cards; box: six outward faces. Does not bake lighting or collision.
Build bakeSurfaceProxy(const Model& source, const Json& options);
}
