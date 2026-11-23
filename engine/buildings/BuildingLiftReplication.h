#pragma once
// Building lift state payload carried inside the game's existing authorized
// snapshot transport. This file does NOT open sockets or allocate a second
// replication service. It is a bounded little-endian codec with strict
// validation, scene identity and increasing server tick/revision checks.
//
// Server -> client ONLY: do not accept this payload as a client command.
// Requests to interact with lifts must go through the game's established
// actor-ownership/permission path and authoritative circulation owner.
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include "engine/buildings/BuildingCirculationHost.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
using lib::systems::BuildingLiftSnapshot;
using lib::systems::BuildingLiftPhase;

struct BuildingLiftPassengerWire {
    uint64_t actorId=0;
    uint32_t liftFeature=0;
};
struct BuildingLiftStateFrame {
    uint64_t sceneId=0;
    uint64_t serverTick=0;
    std::vector<BuildingLiftSnapshot> lifts;
    std::vector<BuildingLiftPassengerWire> passengers;
};
namespace liftWireDetail {
constexpr uint32_t kMagic=0x5446494Cu; // LIFT (little endian)
constexpr uint16_t kVersion=1;
constexpr size_t kHeader=4+2+2+8+8+2+2;
constexpr size_t kLiftSize=4+1+1+1+1+8+4+8+4;
constexpr size_t kPassengerSize=8+4;
constexpr size_t kMaxLifts=32;
constexpr size_t kMaxPassengers=256;
constexpr size_t kMaxBytes=kHeader+kMaxLifts*kLiftSize+
                           kMaxPassengers*kPassengerSize;
struct Writer {
    std::vector<uint8_t> data;
    void u8(uint8_t n){data.push_back(n);}
    void u16(uint16_t n){for(int i=0;i<2;++i)u8(uint8_t(n>>(i*8)));}
    void u32(uint32_t n){for(int i=0;i<4;++i)u8(uint8_t(n>>(i*8)));}
    void u64(uint64_t n){for(int i=0;i<8;++i)u8(uint8_t(n>>(i*8)));}
};
struct Reader {
    std::span<const uint8_t> input;
    size_t at=0;
    bool ok=true;
    uint64_t read(int count) {
        if(!ok||count<0||count>8||size_t(count)>input.size()-at){
            ok=false;return 0;
        }
        uint64_t n=0;
        for(int i=0;i<count;++i)n|=uint64_t(input[at++])<<(i*8);
        return n;
    }
    uint8_t u8(){return uint8_t(read(1));}
    uint16_t u16(){return uint16_t(read(2));}
    uint32_t u32(){return uint32_t(read(4));}
    uint64_t u64(){return read(8);}
};
}
inline bool validateBuildingLiftStateFrame(const BuildingLiftStateFrame& frame,
                                           std::string* error=nullptr){
    const auto fail=[&](const char* reason){
        if(error)*error=reason;
        return false;
    };
    if(!frame.sceneId)return fail("Lift state has no scene identity");
    if(frame.lifts.size()>liftWireDetail::kMaxLifts||
       frame.passengers.size()>liftWireDetail::kMaxPassengers)
        return fail("Lift replication entity budget exceeded");
    std::map<uint32_t,const BuildingLiftSnapshot*> known;
    for(const auto& lift:frame.lifts){
        if(lift.phase>BuildingLiftPhase::Offline||
           lift.currentFloor>=8||lift.targetFloor>=8||
           lift.phaseElapsedMs>lib::systems::BuildingLiftController::kDoorTimeMs||
           lift.passengerCount>lib::systems::BuildingLiftController::kMaxPassengers||
           lift.revision==0||lift.platformYmm<0||lift.platformYmm>1000000)
            return fail("Invalid lift snapshot values");
        if(!known.emplace(lift.featureIndex,&lift).second)
            return fail("Duplicate lift feature index");
    }
    std::map<uint64_t,bool> passengerIds;
    std::map<uint32_t,uint32_t> observed;
    for(const auto& rider:frame.passengers){
        if(!rider.actorId||!passengerIds.emplace(rider.actorId,true).second)
            return fail("Empty or duplicated passenger actor ID");
        if(!known.count(rider.liftFeature))
            return fail("Passenger references missing lift");
        if(++observed[rider.liftFeature]>
           lib::systems::BuildingLiftController::kMaxPassengers)
            return fail("Lift passenger count exceeds limit");
    }
    // The controller can also count scripted cargo/non-actor riders. Never
    // infer all occupancy solely from the actor IDs.
    for(const auto& [feature,count]:observed)
        if(count>known[feature]->passengerCount)
            return fail("Passenger identities exceed lift occupancy");
    if(error)error->clear();
    return true;
}
inline bool encodeBuildingLiftStateFrame(const BuildingLiftStateFrame& frame,
                                         std::vector<uint8_t>& data,
                                         std::string* error=nullptr){
    if(!validateBuildingLiftStateFrame(frame,error))return false;
    liftWireDetail::Writer w;
    w.data.reserve(liftWireDetail::kHeader+
        frame.lifts.size()*liftWireDetail::kLiftSize+
        frame.passengers.size()*liftWireDetail::kPassengerSize);
    w.u32(liftWireDetail::kMagic);
    w.u16(liftWireDetail::kVersion);
    w.u16(0); // reserved protocol flags must be zero
    w.u64(frame.sceneId);
    w.u64(frame.serverTick);
    w.u16(uint16_t(frame.lifts.size()));
    w.u16(uint16_t(frame.passengers.size()));
    for(const auto& lift:frame.lifts){
        w.u32(lift.featureIndex);
        w.u8(uint8_t(lift.phase));
        w.u8(uint8_t(lift.currentFloor));
        w.u8(uint8_t(lift.targetFloor));
        w.u8(uint8_t(lift.passengerCount));
        w.u64(uint64_t(lift.platformYmm));
        w.u32(lift.phaseElapsedMs);
        w.u64(lift.revision);
        w.u32(0); // reserved for future travel/door state
    }
    for(const auto& rider:frame.passengers){
        w.u64(rider.actorId);
        w.u32(rider.liftFeature);
    }
    data=std::move(w.data);
    if(error)error->clear();
    return true;
}
inline bool decodeBuildingLiftStateFrame(std::span<const uint8_t> data,
                                         BuildingLiftStateFrame& frame,
                                         std::string* error=nullptr){
    const auto fail=[&](const char* reason){
        if(error)*error=reason;
        return false;
    };
    if(data.size()<liftWireDetail::kHeader||
       data.size()>liftWireDetail::kMaxBytes)
        return fail("Truncated or oversized lift state packet");
    liftWireDetail::Reader rd{data};
    const auto magic=rd.u32(),version=rd.u16(),flags=rd.u16();
    BuildingLiftStateFrame candidate;
    candidate.sceneId=rd.u64();
    candidate.serverTick=rd.u64();
    const auto count=rd.u16(),riders=rd.u16();
    if(!rd.ok||magic!=liftWireDetail::kMagic||
       version!=liftWireDetail::kVersion||flags!=0)
        return fail("Unsupported lift protocol header");
    if(count>liftWireDetail::kMaxLifts||
       riders>liftWireDetail::kMaxPassengers||
       data.size()!=liftWireDetail::kHeader+
           size_t(count)*liftWireDetail::kLiftSize+
           size_t(riders)*liftWireDetail::kPassengerSize)
        return fail("Lift state packet lengths exceed declared bounds");
    candidate.lifts.reserve(count);
    candidate.passengers.reserve(riders);
    for(uint32_t i=0;i<count;++i){
        BuildingLiftSnapshot lift;
        lift.featureIndex=rd.u32();
        lift.phase=BuildingLiftPhase(rd.u8());
        lift.currentFloor=rd.u8();
        lift.targetFloor=rd.u8();
        lift.passengerCount=rd.u8();
        lift.platformYmm=int64_t(rd.u64());
        lift.phaseElapsedMs=rd.u32();
        lift.revision=rd.u64();
        if(rd.u32()!=0)return fail("Nonzero lift reserved flags");
        candidate.lifts.push_back(lift);
    }
    for(uint32_t i=0;i<riders;++i)
        candidate.passengers.push_back({rd.u64(),rd.u32()});
    if(!rd.ok||rd.at!=data.size()||
       !validateBuildingLiftStateFrame(candidate,error))return false;
    frame=std::move(candidate);
    if(error)error->clear();
    return true;
}

// Collect immutable value-copies for the game's existing server snapshot
// channel. Networking owner serializes this using encodeBuildingLiftStateFrame.
// The physics/ECS owner calls this only AFTER publishing cabin and rider
// transforms on the same authoritative simulation tick.
inline BuildingLiftStateFrame captureBuildingLiftServerFrame(
    const BuildingCirculationHost& host,uint64_t sceneId,uint64_t serverTick){
    BuildingLiftStateFrame frame;
    frame.sceneId=sceneId;frame.serverTick=serverTick;
    for(const auto& [feature,lift]:host.lifts()){
        (void)feature;
        frame.lifts.push_back(lift.snapshot());
    }
    for(const auto& [actorId,feature]:host.riderBindings())
        frame.passengers.push_back({actorId,feature});
    return frame;
}

class BuildingLiftReplica {
public:
    // Called only with data verified as originating from the trusted
    // server by the already-existing authenticated network transport.
    // Reject a different scene and stale/equal ticks; do not apply client
    // packets on the authority.
    bool acceptServerFrame(BuildingLiftStateFrame frame,uint64_t sceneId,
                           std::string* error=nullptr){
        auto fail=[&](const char* reason){
            if(error)*error=reason;
            return false;
        };
        if(!validateBuildingLiftStateFrame(frame,error))return false;
        if(frame.sceneId!=sceneId)
            return fail("Replication frame targets a different building scene");
        if(hasFrame_&&frame.serverTick<=latest_.serverTick)
            return fail("Stale or reordered lift server tick");
        if(hasFrame_)for(const auto& lift:frame.lifts) {
            auto it=std::find_if(latest_.lifts.begin(),latest_.lifts.end(),
                [&](const BuildingLiftSnapshot& old){
                    return old.featureIndex==lift.featureIndex;
                });
            if(it!=latest_.lifts.end()&&lift.revision<it->revision)
                return fail("Lift revision moved backwards");
        }
        latest_=std::move(frame);
        hasFrame_=true;
        if(error)error->clear();
        return true;
    }
    bool applyToClient(std::map<uint32_t,lib::systems::BuildingLiftController>& controllers,
                       std::string* error=nullptr)const{
        if(!hasFrame_){
            if(error)*error="No trusted lift snapshot";
            return false;
        }
        for(const auto& state:latest_.lifts){
            auto it=controllers.find(state.featureIndex);
            if(it==controllers.end()){
                if(error)*error="Client lift feature is unknown";
                return false;
            }
            if(state.revision<=it->second.snapshot().revision)continue;
            if(!it->second.applyReplicatedSnapshot(state)){
                if(error)*error="Client rejected invalid lift state";
                return false;
            }
        }
        if(error)error->clear();
        return true;
    }
    const BuildingLiftStateFrame* latest()const noexcept{
        return hasFrame_?&latest_:nullptr;
    }
    void clear(){hasFrame_=false;latest_={};}
private:
    bool hasFrame_=false;
    BuildingLiftStateFrame latest_;
};
} // namespace aiverse::building
