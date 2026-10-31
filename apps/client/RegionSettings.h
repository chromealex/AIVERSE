#pragma once
#include "engine/platform/Platform.h"
#include <fstream>
namespace rts::client {
inline int loadRegionRatio(){int ratio=50;std::ifstream f(platform::userDataDir()+"/region-ratio.cfg");f>>ratio;return ratio==35||ratio==100?ratio:50;}
inline void saveRegionRatio(int ratio){std::ofstream f(platform::userDataDir()+"/region-ratio.cfg");f<<ratio;}
}
