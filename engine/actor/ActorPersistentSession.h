#pragma once
#include "ActorDurableComponents.h"
#include "engine/world/NativeWorldFinality.h"
#include "engine/world/UnifiedComponentSection.h"
#include "engine/world/UnifiedResourceSection.h"

namespace aiverse::actor {
// Owner-scoped persistence for the explicitly supported actor
// projection. All callbacks operate on private ECS state until publication.
class ActorPersistentSession {
    using Finality=world::NativeWorldFinality;
    using State=world::UnifiedWorldReplay::State;
    Simulation& sim_;
    ActorHost& host_;
    world::DurableComponentCodecs codecs_;
    PersistentEcsRegistry registry_;
    world::UnifiedWalSession session_;
    std::unique_ptr<Finality> finality_;
    std::string identity_;
    size_t budget_;
    bool failed_=false,checkpointPending_=false,initialized_=false;
    bool asyncAfterRecovery_=false;
    uint64_t compactThrough_=0;
    size_t compactLimit_=0;
    static bool coverage(const World& w,const world::RegionEntityLedger& ledger){
        using namespace aiverse::detail;
        const std::set<uint32_t> components{
            componentTypeId<CharacterId>(),componentTypeId<CharacterBody>(),componentTypeId<CharacterHealth>(),
            componentTypeId<CharacterWeapons>(),componentTypeId<CharacterInventory>(),componentTypeId<CharacterControl>(),
            componentTypeId<CharacterAction>(),componentTypeId<CharacterMelee>(),componentTypeId<BotMemory>(),
            componentTypeId<LocomotionBody>(),componentTypeId<lib::systems::GearLoadout>(),
            componentTypeId<Vehicle>(),componentTypeId<Loot>(),componentTypeId<Projectile>(),
            componentTypeId<vehicles::VehicleDrive>(),componentTypeId<vehicles::VehicleOccupant>(),
            componentTypeId<world::RegionStableIdentity>()
        };
        auto derived=ActorHost::portableDerivedResourceTypes();
        std::set<uint32_t> resources(derived.begin(),derived.end());
        for(auto id:{componentTypeId<StateResource>(),componentTypeId<vehicles::RaceState>(),
            componentTypeId<world::RegionMoveRequests>(),componentTypeId<world::RegionDirtyQueue>(),
            componentTypeId<world::RegionEntityLedger>()})resources.insert(id);
        for(auto id:w.activeComponentTypes())if(!components.contains(id))return false;
        for(auto id:w.activeResourceTypes())if(!resources.contains(id))return false;
        const auto entities=w.entityState();
        return entities.pendingDestroy.empty()&&
            size_t(std::count(entities.alive.begin(),entities.alive.end(),uint8_t(1)))==ledger.liveCount();
    }
    using StateResource=actor::State;
    world::NativeRestartState::CommandPolicy commandPolicy()const{
        const auto slots=sim_.players().size();
        return [slots](const Command& c){
            Input input;
            return c.player&&c.player<=slots&&c.seq&&c.flags==0&&c.param==0&&
                c.point==Vec3{}&&c.target==kNullEntity&&decodeInput(c,input);
        };
    }
    bool validate(const State& s)const{
        world::NativeRestartState::Data meta;
        if(!coverage(s.world,s.ledger)||!ActorDurableComponents::supportedWorld(s.world)||
           !world::NativeRestartState::decode(s.restart,meta)||meta.contentIdentity!=identity_||
           meta.seed!=sim_.config().seed||meta.tickRate!=sim_.tickRate()||
           meta.players.size()!=sim_.players().size())return false;
        for(size_t i=0;i<meta.players.size();++i)
            if(meta.players[i].slot!=sim_.players()[i].slot||meta.players[i].team!=sim_.players()[i].team)return false;
        SimSnapshot staged;
        return host_.validatePortableWorld(s.world,meta.tick)&&
            world::NativeRestartState::stage(sim_,s.world,s.ledger,s.restart,identity_,commandPolicy(),staged);
    }
    auto validator(){return [this](const State& s){return validate(s);};}
    world::UnifiedCheckpointSections::Adapters adapters()const{
        return {world::UnifiedEntitySection::adapter(ActorDurableComponents::spawnShell),
            world::UnifiedComponentSection::adapter(codecs_,budget_),
            world::UnifiedResourceSection::adapter(ActorDurableComponents::resourcePolicy(registry_),budget_)};
    }
    bool initialize(){
        if(initialized_||failed_||finality_||!budget_||identity_.empty()||sim_.tick()!=0||sim_.hasCommandSink())return false;
        const auto* ledger=sim_.world().resourceConst<world::RegionEntityLedger>();
        if(!ledger||!coverage(sim_.world(),*ledger)||!ActorDurableComponents::registerCodecs(codecs_)||
           !codecs_.addCaptureValidator(coverage)){failed_=true;return false;}
        ActorDurableComponents::registerResources(registry_);registry_.seal();
        initialized_=true;return true;
    }
    bool finishStart(bool resume,bool async){
        if(resume){
            const auto* recovered=session_.state();
            SimSnapshot staged;
            if(!recovered||!validate(*recovered)||!world::NativeRestartState::stage(sim_,recovered->world,
                recovered->ledger,recovered->restart,identity_,commandPolicy(),staged)){
                failed_=true;session_.close();return false;
            }
            // Enable the worker before publishing live state so setup failure
            // cannot expose a half-started session.
            if(async&&!session_.enableAsync(std::max(budget_,world::RegionWalCodec::maxPayload+world::RegionWalCodec::headerSize))){
                failed_=true;session_.close();return false;
            }
            sim_.loadState(staged);
        }else if(async&&!session_.enableAsync(std::max(budget_,world::RegionWalCodec::maxPayload+world::RegionWalCodec::headerSize))){
            failed_=true;session_.close();return false;
        }
        finality_=std::make_unique<Finality>(sim_,session_,codecs_,ActorDurableComponents::resourcePolicy(registry_),
            ActorDurableComponents::materialize,validator(),budget_,identity_,commandPolicy());
        return true;
    }
public:
    using RecoveryResult=world::UnifiedWalSession::RecoveryResult;
    using RecoveryProgress=world::UnifiedWalSession::RecoveryProgress;
    ActorPersistentSession(Simulation& sim,ActorHost& host,std::string identity,size_t budget)
        :sim_(sim),host_(host),identity_(std::move(identity)),budget_(budget){}
    ActorPersistentSession(const ActorPersistentSession&)=delete;
    ActorPersistentSession& operator=(const ActorPersistentSession&)=delete;
    bool start(const std::filesystem::path& directory,const std::array<uint8_t,16>& uuid,bool resume,bool async=false){
        if(!initialize())return false;
        const auto policy=ActorDurableComponents::resourcePolicy(registry_);
        if(resume){
            if(session_.openCheckpoint(directory,uuid,budget_,adapters(),
                [](){return std::make_unique<State>();},codecs_,ActorDurableComponents::materialize,
                validator(),false,policy)!=world::RegionWalWrite::Ok){failed_=true;return false;}
        }else{
            const auto* ledger=sim_.world().resourceConst<world::RegionEntityLedger>();
            State initial;initial.ledger=*ledger;initial.world=sim_.world();
            initial.world.eraseResource<world::RegionEntityLedger>();
            if(!world::NativeRestartState::capture(sim_,*ledger,identity_,commandPolicy(),initial.restart)||
               !validate(initial)||session_.create(directory,uuid,initial,adapters(),budget_)!=world::RegionWalWrite::Ok){
                failed_=true;session_.close();return false;
            }
        }
        return finishStart(resume,async);
    }
    RecoveryResult beginRecovery(const std::filesystem::path& directory,const std::array<uint8_t,16>& uuid,bool async=false){
        if(!initialize())return RecoveryResult::Failed;
        asyncAfterRecovery_=async;
        // Immutable values captured on the owner, not a Simulation or ActorHost
        // pointer. The worker's graph/schema validation is private; configured
        // host content validation runs once on the owner before publication.
        const auto identity=identity_;const auto seed=sim_.config().seed;
        const auto rate=sim_.tickRate();const auto players=sim_.players();
        auto offline=[identity,seed,rate,players](const State& s){
            world::NativeRestartState::Data meta;
            if(!coverage(s.world,s.ledger)||!ActorDurableComponents::supportedWorld(s.world)||
               !world::NativeRestartState::decode(s.restart,meta)||meta.contentIdentity!=identity||
               meta.seed!=seed||meta.tickRate!=rate||meta.players.size()!=players.size())return false;
            for(size_t i=0;i<players.size();++i)
                if(meta.players[i].slot!=players[i].slot||meta.players[i].team!=players[i].team)return false;
            return true;
        };
        auto result=session_.beginRecovery(directory,uuid,budget_,adapters(),
            [](){return std::make_unique<State>();},codecs_,ActorDurableComponents::materialize,
            offline,ActorDurableComponents::resourcePolicy(registry_));
        if(result!=RecoveryResult::Loading)failed_=true;
        return result;
    }
    RecoveryProgress recoveryProgress()const{return session_.recoveryProgress();}
    bool recovering()const{return session_.recovering();}
    void cancelRecovery(){session_.requestRecoveryCancel();}
    RecoveryResult pollRecovery(){
        if(!session_.recovering())return RecoveryResult::Idle;
        const auto result=session_.pollRecovery(validator());
        if(result==RecoveryResult::Loading)return result;
        if(result!=RecoveryResult::Ready){failed_=true;return result;}
        if(!finishStart(true,asyncAfterRecovery_)){failed_=true;return RecoveryResult::Failed;}
        return RecoveryResult::Ready;
    }
    Finality::Result step(){
        if(!finality_||failed_||checkpointPending_)return Finality::Result::NeedsRecovery;
        auto result=finality_->step();
        if(result==Finality::Result::NeedsRecovery)failed_=true;
        return result;
    }
    // True means accepted; in async mode the caller must poll checkpoint
    // completion separately and may not start a new tick until acknowledged.
    // Keep a recent revision window for incremental observers; zero retains
    // no old tombstones at this checkpoint. UINT64_MAX disables compaction.
    bool checkpoint(uint64_t retainRevisions=65536,size_t compactLimit=world::RegionEntityLedger::kMaxEntities){
        if(failed_||!finality_||session_.ioPending()||sim_.tick()!=finality_->lastFinalizedTick())return false;
        const auto* m=session_.manifest();
        const auto* saved=session_.state();
        const auto* live=sim_.world().resourceConst<world::RegionEntityLedger>();
        if(!m||m->generation==UINT64_MAX||!saved||!live||!live->sameState(saved->ledger)){
            failed_=true;return false;
        }
        compactThrough_=live->lastRevision()>retainRevisions?live->lastRevision()-retainRevisions:0;
        compactLimit_=compactLimit;
        if(session_.asyncEnabled()){
            checkpointPending_=session_.queueCheckpoint(m->generation+1,adapters(),budget_,compactThrough_,compactLimit_)==world::UnifiedWalSession::IoResult::Pending;
            if(!checkpointPending_)failed_=true;
            return checkpointPending_;
        }
        if(session_.checkpoint(m->generation+1,adapters(),budget_,compactThrough_,compactLimit_)!=world::RegionCheckpointPublishResult::Ok){
            failed_=true;return false;
        }
        return adoptCompactedLedger();
    }
    bool pollCheckpoint(){
        if(failed_)return false;
        if(!checkpointPending_)return true;
        const auto result=session_.pollIo();
        if(result==world::UnifiedWalSession::IoResult::Pending)return false;
        checkpointPending_=false;
        if(result!=world::UnifiedWalSession::IoResult::Checkpointed){failed_=true;return false;}
        return adoptCompactedLedger();
    }
private:
    bool adoptCompactedLedger(){
        try{
            const auto* saved=session_.state();
            const auto* live=sim_.world().resourceConst<world::RegionEntityLedger>();
            if(!saved||!live){failed_=true;return false;}
            if(!live->sameState(saved->ledger)){
                auto staged=*live;
                staged.compactRetired(compactThrough_,compactLimit_);
                if(!staged.sameState(saved->ledger)){failed_=true;return false;}
                sim_.world().setResource<world::RegionEntityLedger>(std::move(staged));
            }
            compactThrough_=0;compactLimit_=0;return true;
        }catch(...){failed_=true;return false;}
    }
public:
    bool checkpointPending()const{return checkpointPending_;}
    bool pending()const{return session_.ioPending();}
    bool tickPending()const{return session_.ioPending()&&!checkpointPending_;}
    bool failed()const{return failed_;}
    uint32_t finalizedTick()const{return finality_?finality_->lastFinalizedTick():0;}
};
}
