#pragma once
#include "UnifiedWalSession.h"
#include "engine/sim/Simulation.h"
#include <tuple>

namespace aiverse::world {
// Synchronous opt-in owner for a non-rollback authoritative native tick loop.
// Not a gameplay system, not an ECS resource and never called by speculative
// Simulation::step() or a rollback resimulation loop.
class NativeWorldFinality {
public:
    enum class Result { Committed, NoChanges, WaitingForContent, NeedsRecovery };
    using State=UnifiedWorldReplay::State;
    using Record=DurableComponentCodecs::Record;
    using Resources=DurableResourceTransaction;
private:
    Simulation& sim_;
    UnifiedWalSession& session_;
    const DurableComponentCodecs& codecs_;
    const Resources::Policy resources_;
    UnifiedWorldReplay::Materializer materialize_;
    UnifiedWorldReplay::Validator validate_;
    size_t captureBudget_;
    uint32_t lastTick_;
    const std::string restartContent_;
    const NativeRestartState::CommandPolicy restartCommands_;
    bool failed_=false;
    bool running_=false;

    struct Projection {
        std::vector<RegionEntityId> materialized;
        std::vector<Record> components;
    };
    static bool sameLedger(const RegionEntityLedger& a,const RegionEntityLedger& b){
        return a.lastRevision()==b.lastRevision()&&a.records()==b.records();
    }
    static auto key(const Record& r){return std::tie(r.chunk.owner,r.chunk.schema);}
    static bool same(const Record& a,const Record& b){
        return a.operation==b.operation&&key(a)==key(b)&&
            a.chunk.version==b.chunk.version&&a.chunk.bytes==b.chunk.bytes;
    }
    bool project(const World& world,const RegionEntityLedger& ledger,Projection& out)const{
        Projection staged;
        PersistentEntityReferences refs;
        if(!refs.rebuild(world,ledger))return false;
        for(auto id:ledger.liveIds()){
            Entity e;if(refs.resolve(world,id,e))staged.materialized.push_back(id);
        }
        if(!codecs_.captureAll(world,ledger,captureBudget_,staged.components))return false;
        // captureAll is ordered by stable owner and schema, never runtime handle.
        out=std::move(staged);return true;
    }
    bool resourceChanges(const World& before,const World& after,std::vector<Resources::Record>& out)const{
        if(resources_.selected.empty()){out.clear();return true;}
        // The explicit host-owned codec registry may be separate from native
        // rollback registration; neither registry owns gameplay state.
        return resources_.registry&&
            std::find(resources_.selected.begin(),resources_.selected.end(),"aiverse.world.region_entity_ledger")==resources_.selected.end()&&
            Resources::captureChanges(*resources_.registry,resources_.selected,before,after,out);
    }
    bool equalProjection(const World& a,const RegionEntityLedger& al,const World& b,const RegionEntityLedger& bl)const{
        if(!sameLedger(al,bl))return false;
        Projection x,y;
        if(!project(a,al,x)||!project(b,bl,y)||x.materialized!=y.materialized||
            x.components.size()!=y.components.size())return false;
        for(size_t i=0;i<x.components.size();++i)if(!same(x.components[i],y.components[i]))return false;
        std::vector<Resources::Record> changes;
        return resourceChanges(a,b,changes)&&changes.empty();
    }
    static bool componentChanges(const Projection& before,const Projection& after,std::vector<Record>& out){
        std::vector<Record> staged;size_t i=0,j=0,used=12;
        auto append=[&](Record r){
            const size_t cost=19+r.chunk.schema.size()+r.chunk.bytes.size();
            if(cost>DurableComponentTransaction::MaxFrame-used||
               staged.size()>=DurableComponentTransaction::MaxRecords)return false;
            used+=cost;staged.push_back(std::move(r));return true;
        };
        while(i<before.components.size()||j<after.components.size()){
            if(j==after.components.size()||(i<before.components.size()&&key(before.components[i])<key(after.components[j]))){
                auto r=before.components[i++];
                // Lifecycle owner removes an entity and its pools. Only a
                // component removed from a still-materialized owner needs an op.
                if(std::binary_search(after.materialized.begin(),after.materialized.end(),r.chunk.owner)){
                    r.operation=DurableComponentCodecs::Operation::Remove;r.chunk.bytes.clear();
                    if(!append(std::move(r)))return false;
                }
            }else if(i==before.components.size()||key(after.components[j])<key(before.components[i])){
                if(!append(after.components[j++]))return false;
            }else{
                if(!same(before.components[i],after.components[j])&&!append(after.components[j]))return false;
                ++i;++j;
            }
        }
        out.swap(staged);return true;
    }
    bool restartBaseline(const State& durable,const RegionEntityLedger& ledger,std::string& before)const{
        if(restartContent_.empty())return durable.restart.empty();
        if(durable.restart.empty()||!NativeRestartState::capture(sim_,ledger,restartContent_,restartCommands_,before))return false;
        NativeRestartState::Data old,live;
        if(!NativeRestartState::decode(durable.restart,old)||!NativeRestartState::decode(before,live))return false;
        // New authoritative commands may be queued between ticks. Persist the
        // complete accepted queue and sequence counters at the next commit.
        old.pending.clear();live.pending.clear();old.sequences.clear();live.sequences.clear();
        std::string a,b;
        return NativeRestartState::encode(old,a)&&NativeRestartState::encode(live,b)&&a==b;
    }
    Result fail()noexcept{failed_=true;return Result::NeedsRecovery;}
public:
    NativeWorldFinality(Simulation& sim,UnifiedWalSession& session,const DurableComponentCodecs& codecs,
        Resources::Policy resources,UnifiedWorldReplay::Materializer materialize,
        UnifiedWorldReplay::Validator validate={},size_t captureBudget=64u*1024u*1024u,
        std::string restartContent={},NativeRestartState::CommandPolicy restartCommands={})
        :sim_(sim),session_(session),codecs_(codecs),resources_(std::move(resources)),
         materialize_(std::move(materialize)),validate_(std::move(validate)),
         captureBudget_(captureBudget),lastTick_(sim.tick()),restartContent_(std::move(restartContent)),
         restartCommands_(std::move(restartCommands)){}
    NativeWorldFinality(const NativeWorldFinality&)=delete;
    NativeWorldFinality& operator=(const NativeWorldFinality&)=delete;
    bool failed()const{return failed_;}
    uint32_t lastFinalizedTick()const{return lastTick_;}
    // Caller must be the authoritative tick owner, publish no results before
    // success, and stop on NeedsRecovery. The live Simulation is NOT rewound on
    // IO/validation failure; recovery must happen through the host restore path.
    Result step(){
        if(running_)return fail();
        if(failed_||!session_.managedReady()||!materialize_||!captureBudget_||
           sim_.tick()!=lastTick_||lastTick_==UINT32_MAX)return fail();
        running_=true;
        struct Guard{bool& value;~Guard(){value=false;}} guard{running_};
        try{
            const State* durable=session_.state();
            const auto* live=sim_.world().resourceConst<RegionEntityLedger>();
            if(!durable||!live||!equalProjection(durable->world,durable->ledger,sim_.world(),*live))return fail();
            std::string restartBefore,restartAfter;
            if(!restartBaseline(*durable,*live,restartBefore))return fail();
            // Detach the COW resource before starting the capture. Mutations on
            // another copy/thread or resource replacement are checked by exact replay.
            auto* writable=sim_.world().resource<RegionEntityLedger>();
            if(!writable)return fail();
            RegionEntityLedger::MutationCapture capture(*writable);
            if(!capture.ok())return fail();
            sim_.step();
            capture.stop();
            if(failed_)return fail();
            live=sim_.world().resourceConst<RegionEntityLedger>();
            if(!live||!capture.ok())return fail();
            if(!restartContent_.empty()&&!NativeRestartState::capture(sim_,*live,restartContent_,restartCommands_,restartAfter))return fail();
            if(sim_.tick()==lastTick_){
                if(restartBefore!=restartAfter)return fail();
                if(!capture.records().empty()||!equalProjection(durable->world,durable->ledger,sim_.world(),*live))return fail();
                return Result::WaitingForContent;
            }
            if(sim_.tick()!=lastTick_+1||capture.initialRevision()!=durable->ledger.lastRevision())return fail();
            Projection before,after;
            if(!project(durable->world,durable->ledger,before)||!project(sim_.world(),*live,after))return fail();
            std::vector<Record> components;
            std::vector<Resources::Record> resources;
            if(!componentChanges(before,after,components)||!resourceChanges(durable->world,sim_.world(),resources))return fail();
            if(capture.records().empty()&&components.empty()&&resources.empty()&&restartAfter.empty()){
                if(!equalProjection(durable->world,durable->ledger,sim_.world(),*live))return fail();
                lastTick_=sim_.tick();return Result::NoChanges;
            }
            if(durable->sequence==UINT64_MAX)return fail();
            RegionWalFrame frame;
            if(!UnifiedWorldTransaction::encodeCaptured(capture.initialRevision(),live->lastRevision(),
                capture.records(),components,resources,durable->sequence+1,frame,restartAfter))return fail();
            // Session validates the privately replayed candidate BEFORE durable
            // append. Missing mutation capture, unsupported residency changes,
            // bad codecs or a divergent host materializer therefore fail closed.
            auto check=[&](const State& candidate){
                return candidate.restart==restartAfter&&equalProjection(candidate.world,candidate.ledger,sim_.world(),*live)&&
                    (!validate_||validate_(candidate));
            };
            if(session_.commit(frame,codecs_,materialize_,check,resources_)!=RegionWalWrite::Ok)return fail();
            lastTick_=sim_.tick();return Result::Committed;
        }catch(...){return fail();}
    }
};
}
