// CIRCUIT -> game objects: generic switches and powered device state.
// Genre-neutral ECS only: lights, doors, pumps and motors are external
// consumers of this state, never duplicated here.
#pragma once
#include "CircuitEcs.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::circuit {
inline constexpr std::size_t kMaxDevices=8192,kMaxSwitches=8192;
enum class DeviceKind:std::uint8_t {
    Generic=0,Light=1,Door=2,Motor=3,Pump=4,Alarm=5
};
struct Switch {
    Entity node=kNullEntity;
    bool closed=false,enabled=true;
};
// Bind any game object to a CIRCUIT node. Active is a derived ECS
// result, not a second door/light transform or renderer state.
struct Device {
    Entity node=kNullEntity;
    DeviceKind kind=DeviceKind::Generic;
    bool requireSignal=true,enabled=true;
    bool active=false,valid=false;
    std::uint32_t sampledTick=UINT32_MAX;
    std::uint32_t changedTick=UINT32_MAX;
    std::uint32_t transition=0;
};
inline bool valid(const Device& d){
    return static_cast<std::uint8_t>(d.kind)<=5;
}
inline void io(Archive& a,Switch& s){a(s.node,s.closed,s.enabled);}
inline void io(Archive& a,Device& d){
    a(d.node,d.kind,d.requireSignal,d.enabled,d.active,d.valid,
      d.sampledTick,d.changedTick,d.transition);
    if(!a.writing()&&!valid(d))a.fail();
}
// Authoritative input command. This mutates only a switch, not the
// corresponding Node; node.drive is written by synchronizeSwitches in
// a deterministic phase preceding circuit.power_logic.
inline bool setSwitch(World& world,Entity switchEntity,bool closed){
    const auto* original=static_cast<const World&>(world)
                             .tryGetConst<Switch>(switchEntity);
    if(!original||!original->enabled||!world.alive(original->node)||
       !world.has<Node>(original->node))return false;
    world.get<Switch>(switchEntity).closed=closed;
    return true;
}
inline bool synchronizeSwitches(World& world){
    const World& read=world;
    if(read.countExisting<Switch>()>kMaxSwitches)return false;
    std::vector<std::pair<Entity,Switch>> switches;
    read.eachExistingConst<Switch>([&](Entity e,const Switch& sw){
        if(read.alive(e)&&sw.enabled)switches.push_back({e,sw});
    });
    std::sort(switches.begin(),switches.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    // One source owns one drive input. Duplicate enabled switches for one
    // Node are configuration errors; fail before writing any drives.
    std::vector<std::pair<Entity,bool>> drives;
    for(const auto& [id,switchState]:switches){
        (void)id;
        if(!read.alive(switchState.node)||
           !read.has<Node>(switchState.node))continue;
        const auto it=std::find_if(drives.begin(),drives.end(),
            [&](const auto& pair){return pair.first==switchState.node;});
        if(it!=drives.end())return false;
        drives.push_back({switchState.node,switchState.closed});
    }
    if(drives.empty())return true;
    if(!world.prepareExistingWrite<Node>())return false;
    for(const auto& [node,value]:drives)
        world.get<Node>(node).drive=value;
    return true;
}
// After CIRCUIT runs, derive actuator outputs from the real Node pool.
// The host sees edge transitions through changedTick+transition, stable
// under save/rollback and independent of render frame rate.
inline bool updateDevices(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Device>()>kMaxDevices)return false;
    std::vector<std::pair<Entity,Device>> devices;
    read.eachExistingConst<Device>([&](Entity e,const Device& d){
        if(read.alive(e))devices.push_back({e,d});
    });
    std::sort(devices.begin(),devices.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(devices.empty())return true;
    for(auto& [entity,device]:devices){
        (void)entity;
        if(!valid(device))return false;
        if(device.sampledTick==tick)continue;
        const auto* input=read.tryGetConst<Node>(device.node);
        const bool available=device.enabled&&input&&input->enabled&&
                             input->sampledTick==tick;
        const bool active=available&&input->powered&&
                          (!device.requireSignal||input->signal);
        if(device.active!=active){
            if(device.transition==UINT32_MAX)return false;
            ++device.transition;
            device.changedTick=tick;
        }
        device.active=active;
        device.valid=available;
        device.sampledTick=tick;
    }
    if(!world.prepareExistingWrite<Device>())return false;
    for(const auto& [entity,device]:devices)world.get<Device>(entity)=device;
    return true;
}
inline void registerDevicePersistence(Simulation& sim){
    sim.registerPersistentComponent<Switch>("circuit.switch",1);
    sim.registerPersistentComponent<Device>("circuit.device",1);
}
inline void installSwitches(Simulation& sim){
    sim.addSystem("circuit.switch_inputs",[](Simulation& s){
        (void)synchronizeSwitches(s.world());
    });
}
inline void installDevices(Simulation& sim){
    sim.addSystem("circuit.device_outputs",[](Simulation& s){
        (void)updateDevices(s.world(),s.tick());
    });
}
} // namespace aiverse::circuit
