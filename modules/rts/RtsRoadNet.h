// Compatibility: spline roads are shared engine code (engine/roads/RoadNet.h, namespace aiverse::roads; docs/roads.md),
// usable by every project and client. RTS code keeps its old include and unqualified names.
#pragma once
#include "../../engine/roads/RoadNet.h"
namespace aiverse::game { using namespace aiverse::roads; }
