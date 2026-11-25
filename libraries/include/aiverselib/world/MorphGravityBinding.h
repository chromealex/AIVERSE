// Optional persistent configuration, not a second force/position store.
#pragma once
#include "MorphEcs.h"

namespace aiverse::morph {
struct GravityBinding {
    Entity frameOwner=kNullEntity;
    int32_t unitsPerMetre=100;
    int32_t impulseMilli=0; // 1000 = one node impulse unit per m/s^2
    bool relative=true; // subtract owner's acceleration for free-fall frames
    bool enabled=true;
};
inline bool valid(const GravityBinding& b){
    return b.frameOwner.valid()&&b.unitsPerMetre>=1&&b.unitsPerMetre<=1000000&&
        b.impulseMilli>=0&&b.impulseMilli<=1000000;
}
inline void io(Archive& a,GravityBinding& b){
    a(b.frameOwner,b.unitsPerMetre,b.impulseMilli,b.relative,b.enabled);
    if(!a.writing()&&!valid(b))a.fail();
}
inline bool bindGravity(World& world,Entity node,GravityBinding binding){
    if(!world.alive(node)||!world.has<Node>(node)||!world.alive(binding.frameOwner)||
       !valid(binding))return false;
    if(world.has<GravityBinding>(node))world.get<GravityBinding>(node)=binding;
    else world.add<GravityBinding>(node,binding);
    return true;
}
inline void registerGravityPersistence(Simulation& sim){
    sim.registerPersistentComponent<GravityBinding>("morph.gravity_binding",1);
}
} // namespace aiverse::morph
