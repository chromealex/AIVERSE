// TerraWeave trees and rocks on cube-sphere bodies (presentation only, nothing here touches the simulation).
// One site per surface cell: TerraWeave picks the plant from the sphere climate (engine/world/TerraWeaveSphere.h),
// Arbor and the rock generator make the shared prototypes; the renderer instances them.
// Body visual keys: "flora": {"density": 0..1 (0.22), "scale": world units per metre (0.06)} or "flora": false.
#pragma once
#include "GamePlanet.h"
#include "engine/render/Model.h"
#include <memory>
#include <map>
#include <vector>

namespace aiverse::game {

// Value-only completed simulation snapshot, no World or GPU pointers.
struct PlanetForestVisual {
    uint32_t species=0;
    uint64_t key=0;
    int body=-1,proto=0;
    render::float3 local{},up{},forward{};
    float size=1,foliage=1,thickness=1;
    render::float3 tint{1,1,1};
    bool fallen=false,physical=false;
};
struct PlanetForestVisualSnapshot {
    std::vector<uint8_t> managedBodies;
    std::vector<PlanetForestVisual> plants;
};
// Null visibility = unrestricted native/spectator view. Non-null is the raw
// canonical stacked-grid vision (0 unknown, 1 explored, 2 currently visible).
// Missing/malformed restricted vision publishes no current plant records.
// No last-seen history is retained by this value-only snapshot.
bool collectPlanetForestVisuals(const World& world,const Surface& surface,
                              PlanetForestVisualSnapshot& output,
                              const std::vector<uint8_t>* visibility=nullptr);

using PlanetForestModels=std::map<uint32_t,std::unique_ptr<render::Model>>;
// CPU-only shared ARBOR recipe build; no scatter, no GPU state.
PlanetForestModels buildPlanetForestSpecies(const Json& manifest,const render::FileReader& reader={});

class PlanetFlora {
public:
    struct Instance {
        int body = 0;
        int proto = 0;
        render::float3 local;   // body-local position on the ground
        render::float3 up;      // surface direction (unit)
        float yaw = 0, scale = 1;
    };
    void shutdown(gfx::Device& dev);
    // Called at map publication boundary, not from per-frame terrain rebuild.
    void setAuthoredSpecies(gfx::Device& dev,const Json& manifest,const render::FileReader& reader);
    const render::Model* forestProto(uint32_t species,int fallback)const {
        const auto it=authored_.find(species);
        return it==authored_.end()?proto(fallback):it->second.get();
    }
    bool ensurePrototypes(gfx::Device& dev){return makePrototypes(dev);}
    static bool treeProto(int proto){return proto>=0&&proto<5;}
    // Rebuilds the sites of every surface body; prototypes are made and uploaded once per device.
    void setWorld(gfx::Device& dev, const PlanetWorld& w, const std::vector<const PlanetSurface*>& surfaces);
    const std::vector<Instance>& instances() const { return instances_; }
    const render::Model* proto(int i) const { return i >= 0 && size_t(i) < protos_.size() ? protos_[size_t(i)].get() : nullptr; }

private:
    bool makePrototypes(gfx::Device& dev);
    std::vector<std::unique_ptr<render::Model>> protos_;
    std::vector<Instance> instances_;
    PlanetForestModels authored_,pendingAuthored_;
};

} // namespace aiverse::game
