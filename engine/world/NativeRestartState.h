#pragma once
#include "engine/sim/Simulation.h"
#include "PersistentEntityReferences.h"

namespace aiverse::world {
// Durable restart data, separate from gameplay ECS resources and native
// rollback snapshots. Command references use stable IDs, never packed Entity.
class NativeRestartState {
public:
    static constexpr size_t MaxBytes=4u*1024u*1024u,MaxCommands=16384,MaxUnits=4096,MaxArgs=4096;
    struct StableCommand {
        uint32_t tick=0,seq=0;
        uint8_t player=0,flags=0;
        uint16_t type=0;
        std::vector<RegionEntityId> units;
        RegionEntityId target=0;
        Vec3 point{};
        int64_t param=0;
        std::vector<int64_t> args;
    };
    struct Data {
        std::string contentIdentity;
        uint64_t seed=0,rngState=0,rngIncrement=0;
        uint32_t tick=0,tickRate=0;
        std::vector<PlayerInfo> players;
        std::map<uint8_t,uint32_t> sequences;
        std::vector<StableCommand> pending;
    };
    // Host must explicitly certify that opaque param/args and command type
    // contain no transient handles or other process-local references.
    using CommandPolicy=std::function<bool(const Command&)>;
private:
    template<class T> static bool boundedVector(Archive& a,std::vector<T>& v,size_t max,size_t minBytes){
        if(a.writing()&&v.size()>max)return false;
        uint32_t n=a.count(v.size(),minBytes);
        if(!a.ok()||n>max)return false;
        if(!a.writing())v.resize(n);
        for(auto& item:v){a(item);if(!a.ok())return false;}
        return true;
    }
    static bool boundedString(Archive& a,std::string& value,size_t max){
        if(a.writing()&&value.size()>max)return false;
        uint32_t n=a.count(value.size());
        if(!a.ok()||n>max||(!a.writing()&&n>a.remaining()))return false;
        if(!a.writing())value.resize(n);
        a.block(value.data(),n);return a.ok();
    }
    static bool commandIo(Archive& a,StableCommand& c){
        a(c.tick,c.seq,c.player,c.flags,c.type,c.target,c.point,c.param);
        return a.ok()&&boundedVector(a,c.units,MaxUnits,8)&&boundedVector(a,c.args,MaxArgs,8);
    }
    static bool dataIo(Archive& a,Data& d){
        uint32_t version=1;a(version);
        if(!a.ok()||version!=1||!boundedString(a,d.contentIdentity,256)||d.contentIdentity.empty())return false;
        a(d.seed,d.rngState,d.rngIncrement,d.tick,d.tickRate);
        if(!a.ok()||!d.tickRate||!(d.rngIncrement&1))return false;
        if(a.writing()&&d.players.size()>255)return false;
        uint32_t count=a.count(d.players.size(),7);
        if(!a.ok()||count>255)return false;
        if(!a.writing())d.players.resize(count);
        for(size_t i=0;i<d.players.size();++i){
            auto& p=d.players[i];uint8_t alive=p.alive?1:0;
            a(p.slot,p.team,alive);
            if(!a.ok()||p.slot!=i+1||alive>1||!boundedString(a,p.name,1024))return false;
            p.alive=alive!=0;
        }
        if(a.writing()&&d.sequences.size()>256)return false;
        count=a.count(d.sequences.size(),5);
        if(!a.ok()||count>256)return false;
        if(a.writing()){
            for(auto [slot,seq]:d.sequences){uint8_t s=slot;a(s,seq);}
        }else{
            d.sequences.clear();int previous=-1;
            for(uint32_t i=0;i<count;++i){
                uint8_t slot=0;uint32_t seq=0;a(slot,seq);
                if(!a.ok()||int(slot)<=previous)return false;
                previous=slot;d.sequences.emplace(slot,seq);
            }
        }
        if(a.writing()&&d.pending.size()>MaxCommands)return false;
        count=a.count(d.pending.size(),52);
        if(!a.ok()||count>MaxCommands)return false;
        if(!a.writing())d.pending.resize(count);
        uint32_t previous=d.tick;
        for(auto& c:d.pending){
            if(!commandIo(a,c)||c.tick<previous||(c.flags&kCmdHasArgs))return false;
            previous=c.tick;
        }
        return a.ok();
    }
public:
    static bool encode(const Data& data,std::string& out){
        // Bound allocation before serialization, even for direct callers.
        if(data.pending.size()>MaxCommands)return false;
        size_t used=0;
        for(const auto& c:data.pending){
            if(c.units.size()>MaxUnits||c.args.size()>MaxArgs)return false;
            const size_t cost=64+8*(c.units.size()+c.args.size());
            if(cost>MaxBytes-used)return false;
            used+=cost;
        }
        if(data.contentIdentity.size()>256||data.players.size()>255||data.sequences.size()>256)return false;
        for(const auto& p:data.players)if(p.name.size()>1024)return false;
        auto staged=data;std::string bytes;Archive a(&bytes);
        if(!dataIo(a,staged)||bytes.size()>MaxBytes)return false;
        out.swap(bytes);return true;
    }
    static bool decode(const std::string& bytes,Data& out){
        if(bytes.empty()||bytes.size()>MaxBytes)return false;
        Archive a(bytes);Data staged;
        if(!dataIo(a,staged)||a.remaining()!=0)return false;
        out=std::move(staged);return true;
    }
    static bool referencesValid(const Data& data,const World& world,const RegionEntityLedger& ledger){
        PersistentEntityReferences refs;if(!refs.rebuild(world,ledger))return false;
        for(const auto& c:data.pending){
            Entity e;
            if(!refs.resolve(world,c.target,e))return false;
            for(auto id:c.units)if(!id||!refs.resolve(world,id,e))return false;
        }
        return true;
    }
    static bool capture(const Simulation& sim,const RegionEntityLedger& ledger,
        const std::string& contentIdentity,const CommandPolicy& policy,std::string& out){
        PersistentEntityReferences refs;
        if(!refs.rebuild(sim.world(),ledger))return false;
        Data d;d.contentIdentity=contentIdentity;d.seed=sim.config().seed;
        d.tick=sim.tick();d.tickRate=sim.tickRate();d.rngState=sim.rng().state();
        d.rngIncrement=sim.rng().increment();d.players=sim.players();d.sequences=sim.commandSequences();
        size_t used=0;
        for(const auto& [tick,commands]:sim.pendingCommands()){
            for(const auto& cmd:commands){
                if(tick!=cmd.tick||tick<d.tick||!policy||!policy(cmd)||
                   cmd.units.size()>MaxUnits||cmd.args.size()>MaxArgs||d.pending.size()>=MaxCommands)return false;
                const size_t cost=64+8*(cmd.units.size()+cmd.args.size());
                if(cost>MaxBytes-used)return false;
                used+=cost;
                StableCommand c;c.tick=cmd.tick;c.seq=cmd.seq;c.player=cmd.player;c.flags=cmd.flags;
                c.type=cmd.type;c.point=cmd.point;c.param=cmd.param;c.args=cmd.args;
                if(!refs.encode(sim.world(),cmd.target,c.target))return false;
                for(auto e:cmd.units){
                    RegionEntityId id=0;
                    if(!refs.encode(sim.world(),e,id)||!id)return false;
                    c.units.push_back(id);
                }
                d.pending.push_back(std::move(c));
            }
        }
        return encode(d,out);
    }
    // Prepare metadata in a private snapshot. Host supplies a fully rebuilt
    // World and publishes via its normal restore barrier only after validation.
    // Executed command history is intentionally not retained; it is a replay sink.
    static bool stage(const Simulation& sim,const World& rebuilt,const RegionEntityLedger& ledger,
        const std::string& bytes,const std::string& contentIdentity,const CommandPolicy& policy,SimSnapshot& out){
        Data d;
        if(!decode(bytes,d)||d.contentIdentity!=contentIdentity||d.seed!=sim.config().seed||
           d.tickRate!=sim.tickRate())return false;
        PersistentEntityReferences refs;if(!refs.rebuild(rebuilt,ledger))return false;
        SimSnapshot staged;
        staged.tick=d.tick;staged.rng.restore(d.rngState,d.rngIncrement);
        staged.players=std::move(d.players);staged.seqByPlayer=std::move(d.sequences);
        for(const auto& c:d.pending){
            Command cmd;cmd.tick=c.tick;cmd.seq=c.seq;cmd.player=c.player;cmd.flags=c.flags;
            cmd.type=c.type;cmd.point=c.point;cmd.param=c.param;cmd.args=c.args;
            if(!refs.resolve(rebuilt,c.target,cmd.target))return false;
            for(auto id:c.units){
                Entity e;if(!id||!refs.resolve(rebuilt,id,e))return false;
                cmd.units.push_back(e);
            }
            if(!policy||!policy(cmd))return false;
            staged.pending[cmd.tick].push_back(std::move(cmd));
        }
        staged.world=rebuilt;staged.world.setResource<RegionEntityLedger>(ledger);
        out=std::move(staged);return true;
    }
};
}
