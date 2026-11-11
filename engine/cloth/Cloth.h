#pragma once
#include "engine/render/Math3D.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aiverse::cloth {
using Vec = render::float3;
// Presentation-only XPBD. Units: metres, kg, seconds. No RTS lockstep dependency.
struct Settings {
    int cells=24, substeps=2, iterations=4;
    float width=3, height=3, mass=0.3f;
    float stretchCompliance=1e-7f, bendCompliance=0.002f;
    float damping=1.5f, thickness=0.02f;
};
struct Edge { uint32_t a,b; float rest,compliance; bool bend; };
class Grid {
public:
    explicit Grid(Settings settings);
    const Settings settings;
    std::vector<Vec> rest;
    std::vector<Edge> edges;
};
struct Capsule { Vec a,b; float radius=0.5f; }; // a==b is a sphere; cloth-local coordinates
struct Environment {
    Vec gravity{0,-9.81f,0}, wind{0,0,3}; // wind is a prescribed acceleration, not aerodynamic flow
    float floor=-3.2f;
    std::span<const Capsule> capsules; // at most 32; invalid colliders are ignored
};
class Cloth {
public:
    explicit Cloth(std::shared_ptr<const Grid> grid, Vec origin={});
    void reset();
    void step(float dt,const Environment& environment);
    void movePins(Vec offset); // local offset of the pinned top edge
    const Grid& grid()const{return *grid_;}
    std::span<const Vec> positions()const{return p_;}
    Vec origin()const{return origin_;}
    float maxStretch()const;
    bool healthy()const;
    float windScale=1; // finite host-supplied gust multiplier
    bool active=true; // host-controlled culling; inactive cloth freezes (no hidden catch-up)
private:
    friend class Batch;
    uint64_t fingerprint();
    bool sameState(const Cloth&) const;
    uint64_t fingerprint_=0;
    bool fingerprintValid_=false;
    std::shared_ptr<const uint8_t> proofOwner_;
    uint64_t proofId_=0;
    std::shared_ptr<const Grid> grid_;
    Vec origin_,pinOffset_{};
    std::vector<Vec> p_,previous_,velocity_;
    std::vector<float> lambda_,weights_;
};
// Persistent workers distribute whole cloths. Scratch capacity grows only with object count.
// Owner thread calls step(); do not mutate cloths until it returns. Web always uses one worker.
class Batch {
public:
    struct Stats {size_t activeObjects=0, simulatedObjects=0, copiedObjects=0, deferredObjects=0;};
    explicit Batch(int workers=1,bool shareIdentical=false);
    ~Batch();
    Batch(const Batch&)=delete;
    Batch& operator=(const Batch&)=delete;
    void step(std::span<Cloth> cloths,float dt,const Environment& environment);
    // Per-object inputs are in each cloth's local coordinates. Empty = use common environment.
    void step(std::span<Cloth> cloths,float dt,const Environment& common,std::span<const Environment> perObject);
    // One dt per object; zero defers that object without changing its state.
    void stepScheduled(std::span<Cloth> cloths,std::span<const float> steps,const Environment& common,
                       std::span<const Environment> perObject={});
    void setSharing(bool enabled);
    Stats stats()const;
    std::span<const size_t> representatives()const; // valid until next step; one source index per object
    int workers()const;
private:
    friend class TemporalLod;
    void run(std::span<Cloth>,float,const Environment&,std::span<const Environment>,std::span<const float>,bool execute=true);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
