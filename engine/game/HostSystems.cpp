// GameCore picks the systems that run the whole match from the project's data (HostSystems.h).
// One host for every project; grid systems are not set up when one of these runs (docs/no-genre-split.md).
#include "GameCore.h"
#include "../actor/ActorModule.h"
#include "../voxel/VoxelGame.h"

namespace aiverse::game {
std::shared_ptr<HostSystems> GameCore::makeHostSystems(const Json& rules, const Json& map) {
    if (worldSpaceMap(map)) return std::make_shared<actor::ActorHost>();
    if (rules["voxelGame"].isObject()) return std::make_shared<voxelgame::VoxelHost>();
    return nullptr;
}
}

namespace aiverse::actor {
ActorHost* tryHost(Simulation& sim) {
    auto* core = dynamic_cast<game::GameCore*>(&sim.module());
    return core ? dynamic_cast<ActorHost*>(core->systems()) : nullptr;
}
ActorHost& host(Simulation& sim) {
    if (auto* a = tryHost(sim)) return *a;
    throw std::logic_error("actor::host: the simulation does not run a world-space map");
}
const ActorHost& host(const Simulation& sim) { return host(const_cast<Simulation&>(sim)); }
}

namespace aiverse::voxelgame {
VoxelHost* tryHost(Simulation& sim) {
    auto* core = dynamic_cast<game::GameCore*>(&sim.module());
    return core ? dynamic_cast<VoxelHost*>(core->systems()) : nullptr;
}
VoxelHost& host(Simulation& sim) {
    if (auto* v = tryHost(sim)) return *v;
    throw std::logic_error("voxelgame::host: the simulation does not run a voxel world");
}
}
