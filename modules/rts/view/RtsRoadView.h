// Compatibility: the spline road view is shared engine code (engine/roads/RoadView.h, aiverse::roads::RoadView),
// drawn by every client that has a map "roadNetwork". The RTS view keeps its old name.
#pragma once
#include "../../../engine/roads/RoadView.h"
#include "../RtsRoadNet.h"
namespace aiverse::game { using RtsRoadView = aiverse::roads::RoadView; }
