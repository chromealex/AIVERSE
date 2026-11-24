#pragma once
// A set of systems that runs a whole match for GameCore (the one GameModule) when the project's data asks
// for it: actor systems on world-space maps (engine/actor), voxel worlds (engine/voxel). GameCore owns one
// and hands every GameModule entry point to it (HostSystems.cpp). Not a GameModule: there is one host.
#include "../sim/Simulation.h"

namespace aiverse::game {
class HostSystems {
public:
    virtual ~HostSystems() = default;
    virtual void setup(Simulation&, const Json& rules, const Json& map) = 0;
    virtual void applyCommand(Simulation&, const Command&) = 0;
    virtual void hashState(Simulation&, Hasher&) = 0;
    virtual bool serializeState(Simulation&, Archive&) = 0;
    virtual int result(Simulation&) = 0;
    virtual bool readyToStep(Simulation&) { return true; }
};
}
