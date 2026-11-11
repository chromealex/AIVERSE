#pragma once
#include "engine/data/Json.h"
#include <box3d/box3d.h>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::physics {
// Optional host/authoritative physics, independent of RTS Fixed lockstep.
// Keep one owner thread per World. Do not advance it from rendering or wall-clock decisions.
struct Object {
    std::string id,shape;
    b3BodyId body{};
    b3Vec3 size{1,1,1}; // box full size; sphere/capsule: radius, half-height, unused
    b3Vec3 color{0.5f,0.6f,0.8f};
    bool dynamic=false;
};
class World {
public:
    explicit World(const Json& scene,int workers=1);
    ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    void step(); // scene-defined fixed rate and substeps
    bool impulse(const std::string& id,b3Vec3 impulse);
    const Object* find(const std::string& id) const;
    const std::vector<Object>& objects()const{return objects_;}
    b3WorldId nativeId()const{return world_;}
    uint64_t tick()const{return tick_;}
    int tickRate()const{return tickRate_;}
    // Full upstream seed snapshot including contact caches/joints/sleep state, wrapped with
    // revision/configuration identity + integrity hash. Trusted local saves only, NOT network input.
    std::string save()const;
    bool restore(const std::string& bytes,std::string* error=nullptr);
    // Diagnostic only: transforms/velocities. Not a complete simulation checksum.
    uint64_t trajectoryHash()const;
private:
    void release();
    Json scene_;
    b3WorldId world_{};
    b3RecPlayer* restored_=nullptr;
    std::vector<Object> objects_;
    int workers_=1,tickRate_=60,substeps_=4;
    uint64_t tick_=0;
};
}
