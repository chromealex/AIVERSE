#pragma once
#include "engine/platform/Platform.h"
#include <memory>
#include <string>

namespace aiverse { class Simulation; }
namespace aiverse::render { class GeometryScene; }
namespace aiverse::client {
// The desktop actor client and its local loopback server on a Studio-owned drawable.
class HostedActorClient {
public:
    static std::unique_ptr<HostedActorClient> open(gfx::Device&, const std::string& project,
                                                  const std::string& engineAssets, size_t mapIndex);
    virtual ~HostedActorClient() = default;
    virtual Simulation& simulation() = 0;
    virtual const render::GeometryScene* staticGeometry() const = 0;
    virtual void frame(int width, int height, float logicalWidth, float logicalHeight, double dt) = 0;
    virtual void event(const platform::Event&) = 0;
    virtual bool paused() const = 0;
    virtual void pause(bool paused) = 0;
    virtual void step() = 0;
};
}
