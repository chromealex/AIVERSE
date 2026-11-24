#pragma once
// Genre-independent adapter to the existing admitted net::replica session.
// Projects reserve two message channels, call receive from Server::onMessage,
// and send only to peers entitled to see this scene. No new socket, login,
// actor registry, or simulation authority.
#include "engine/buildings/BuildingWorldRuntime.h"
#include "engine/net/replica/Replica.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
struct BuildingReplicaLeaf {
    uint16_t physicalPermille=0;
    bool open=false,moving=false;
};
struct BuildingReplicaSceneFrame {
    uint64_t sceneId=0,epoch=0,contentHash=0,serverTick=0,revision=0;
    std::vector<BuildingReplicaLeaf> leaves;
    BuildingLiftStateFrame circulation;
    bool hasCirculation=false;
};
namespace buildingReplicaWire {
constexpr uint32_t kMagic=0x4C445542u; // BUDL
constexpr uint16_t kVersion=1;
constexpr size_t kMaxBytes=8192;
constexpr size_t kFixedHeader=4+2+2+8*5+2+4;
inline bool fail(std::string* error,const std::string& why){
    if(error)*error=why;return false;
}
}

// Raw existing Command wire, with exact bounded shape validated BEFORE
// readCommand can allocate client-supplied units/args counts.
inline bool decodeBuildingReplicaCommand(std::span<const uint8_t> bytes,
                                         Command& out,std::string* error=nullptr){
    using buildingReplicaWire::fail;
    if(bytes.size()!=92&&bytes.size()!=100)
        return fail(error,"Invalid building command byte length");
    const auto u32=[&](size_t i){
        return uint32_t(bytes[i])|(uint32_t(bytes[i+1])<<8)|
               (uint32_t(bytes[i+2])<<16)|(uint32_t(bytes[i+3])<<24);
    };
    if(bytes[5]!=kCmdHasArgs||u32(12)!=1||
       u32(64)!=(bytes.size()==92?3u:4u))
        return fail(error,"Invalid building command wire shape");
    ByteReader reader(bytes.data(),bytes.size());
    auto command=readCommand(reader);
    if(!reader.ok()||!reader.atEnd()||!command.seq||command.units.size()!=1)
        return fail(error,"Invalid building command payload");
    out=std::move(command);
    if(error)error->clear();return true;
}
inline bool encodeBuildingReplicaCommand(const Command& command,
                              net::Bytes& out,std::string* error=nullptr){
    if(!command.seq||command.flags||command.units.size()!=1||
       (command.args.size()!=3&&command.args.size()!=4))
        return buildingReplicaWire::fail(error,"Invalid building command envelope");
    ByteWriter writer;writeCommand(writer,command);
    Command verified;
    if(!decodeBuildingReplicaCommand(writer.buf,verified,error))return false;
    out=std::move(writer.buf);return true;
}

// Read-only scene snapshot. Aperture ordinals match the authored sorted
// socket table; the client must match the content hash before applying.
// The lift portion reuses the existing bounded lift replication codec.
inline bool decodeBuildingReplicaScene(std::span<const uint8_t> bytes,
                      BuildingReplicaSceneFrame& out,std::string* error=nullptr){
    using namespace buildingReplicaWire;
    if(bytes.size()<kFixedHeader||bytes.size()>kMaxBytes)
        return fail(error,"Invalid building scene frame length");
    ByteReader r(bytes.data(),bytes.size());
    if(r.u32()!=kMagic||r.u16()!=kVersion||r.u16()!=0)
        return fail(error,"Unsupported building scene frame");
    BuildingReplicaSceneFrame staged;
    staged.sceneId=r.u64();staged.epoch=r.u64();staged.contentHash=r.u64();
    staged.serverTick=r.u64();staged.revision=r.u64();
    const auto count=r.u16();
    if(!staged.sceneId||!staged.epoch||staged.epoch>uint64_t(INT64_MAX)||
       !staged.contentHash||!staged.revision||count>64||
       bytes.size()<kFixedHeader+size_t(count)*3)
        return fail(error,"Invalid building scene frame identity");
    staged.leaves.reserve(count);
    for(uint16_t i=0;i<count;++i){
        BuildingReplicaLeaf leaf;
        leaf.physicalPermille=r.u16();
        const uint8_t flags=r.u8();
        if(flags>3||leaf.physicalPermille>1000)
            return fail(error,"Invalid building leaf progress");
        leaf.open=(flags&1)!=0;leaf.moving=(flags&2)!=0;
        if(leaf.open!=(leaf.physicalPermille==1000))
            return fail(error,"Inconsistent building leaf endpoint");
        staged.leaves.push_back(leaf);
    }
    const uint32_t liftBytes=r.u32();
    if(liftBytes!=bytes.size()-kFixedHeader-size_t(count)*3)
        return fail(error,"Invalid nested lift frame length");
    if(liftBytes){
        auto lift=bytes.last(liftBytes);
        if(!decodeBuildingLiftStateFrame(lift,staged.circulation,error)||
           staged.circulation.sceneId!=staged.sceneId||
           staged.circulation.serverTick!=staged.serverTick)
            return fail(error,"Invalid nested lift frame");
        staged.hasCirculation=true;
    }
    if(!r.ok())return fail(error,"Truncated building scene frame");
    out=std::move(staged);
    if(error)error->clear();return true;
}

// Client-side acceptance only: does not grant gameplay access. Epoch
// changes invalidate old snapshots and pending requests after recovery.
class BuildingReplicaView {
public:
    bool accept(std::span<const uint8_t> bytes,uint64_t expectedScene,
                uint64_t expectedContentHash,size_t expectedSockets,
                std::string* error=nullptr){
        BuildingReplicaSceneFrame candidate;
        if(!decodeBuildingReplicaScene(bytes,candidate,error))return false;
        if(candidate.sceneId!=expectedScene||candidate.contentHash!=expectedContentHash||
           candidate.leaves.size()!=expectedSockets)
            return buildingReplicaWire::fail(error,"Building replica content mismatch");
        if(frame_.sceneId&&(frame_.sceneId!=expectedScene||
           candidate.epoch<frame_.epoch||
           (candidate.epoch==frame_.epoch&&
            (candidate.serverTick<frame_.serverTick||candidate.revision<frame_.revision||
             (candidate.serverTick==frame_.serverTick&&candidate.revision==frame_.revision)))))
            return buildingReplicaWire::fail(error,"Stale building replica frame");
        frame_=std::move(candidate);return true;
    }
    const BuildingReplicaSceneFrame& frame()const noexcept{return frame_;}
    void reset(){frame_={};}
private:
    BuildingReplicaSceneFrame frame_;
};

class BuildingReplicaBridge {
public:
    BuildingReplicaBridge(std::weak_ptr<BuildingWorldRuntime> runtime,
                          uint16_t commandChannel,uint16_t stateChannel)
        :runtime_(std::move(runtime)),command_(commandChannel),state_(stateChannel){}
    bool valid()const noexcept{return command_&&state_&&command_!=state_;}
    bool handles(uint16_t channel)const noexcept{return valid()&&channel==command_;}
    // Only call from an existing admitted Replica Server's onMessage
    // callback. Slot is assigned by that server, never by the payload.
    bool receive(uint8_t slot,uint16_t channel,std::span<const uint8_t> bytes,
                 std::string* error=nullptr)const{
        if(!handles(channel)||!slot)return buildingReplicaWire::fail(error,"Unknown building command channel");
        auto runtime=runtime_.lock();
        Command command;
        if(!runtime||!decodeBuildingReplicaCommand(bytes,command,error))return false;
        return runtime->submitAuthenticated(slot,std::move(command),nullptr,error);
    }
    bool capture(uint64_t sceneId,net::Bytes& out,std::string* error=nullptr)const{
        using namespace buildingReplicaWire;
        auto runtime=runtime_.lock();
        BuildingEcsWorldState record;uint64_t content=0;
        if(!valid()||!runtime||!runtime->captureApertureState(sceneId,record,content))
            return fail(error,"Building state is not committed");
        const uint64_t tick=record.fixedTick;
        net::Bytes lifts;
        if(runtime->hasLifts(sceneId)&&!runtime->captureLiftSnapshot(sceneId,tick,lifts,error))
            return false;
        ByteWriter w;w.u32(kMagic);w.u16(kVersion);w.u16(0);
        w.u64(sceneId);w.u64(runtime->commandEpoch());w.u64(content);
        w.u64(tick);w.u64(record.revision);w.u16(uint16_t(record.leaves.size()));
        for(const auto& leaf:record.leaves){
            w.u16(leaf.physicalPermille);
            w.u8(uint8_t((leaf.open?1:0)|(leaf.moving?2:0)));
        }
        w.u32(uint32_t(lifts.size()));w.buf.insert(w.buf.end(),lifts.begin(),lifts.end());
        BuildingReplicaSceneFrame verified;
        if(!decodeBuildingReplicaScene(w.buf,verified,error))return false;
        out=std::move(w.buf);return true;
    }
    // Explicit unicast only; the host selects authorized interest peers.
    bool send(net::replica::Server& server,uint8_t slot,uint64_t sceneId,
              std::string* error=nullptr)const{
        const auto peers=server.clients();
        if(!slot||std::find(peers.begin(),peers.end(),slot)==peers.end())
            return buildingReplicaWire::fail(error,"Building replica peer is not admitted");
        net::Bytes bytes;
        if(!capture(sceneId,bytes,error))return false;
        server.send(slot,state_,bytes);
        return true;
    }
    uint16_t stateChannel()const noexcept{return state_;}
private:
    std::weak_ptr<BuildingWorldRuntime> runtime_;
    uint16_t command_,state_;
};
} // namespace aiverse::building
