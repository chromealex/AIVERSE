#pragma once
#include "UnifiedWorldReplay.h"
#include "UnifiedCheckpointLoader.h"
#include "RegionWalWriter.h"
#include "RegionWorldSessionLock.h"
#include "UnifiedCheckpointRetention.h"
#include "AsyncPersistenceIo.h"
namespace aiverse::world {
class UnifiedWalSession {
    RegionWorldSessionLock lock_;
    RegionWalWriter writer_;
    std::unique_ptr<UnifiedWorldReplay::State> state_;
    bool ready_=false;
    std::filesystem::path dir_;
    RegionCheckpointManifest manifest_;
    bool managed_=false;
    AsyncPersistenceIo io_;
    bool async_=false;
    enum class Pending { None, Append, Checkpoint } pending_=Pending::None;
    std::unique_ptr<UnifiedWorldReplay::State> prepared_;
    RegionCheckpointManifest preparedManifest_;
    uint64_t ticket_=0;
public:
    using State=UnifiedWorldReplay::State;
    enum class IoResult { Idle, Pending, Committed, Checkpointed, Failed };
    ~UnifiedWalSession(){close();}
    // Caller supplies a verified checkpoint and its exact nonempty identity
    // prefix, including a unified-format discriminator. Existing WAL only.
    RegionWalWrite open(const std::filesystem::path& dir,const std::filesystem::path& wal,
        std::span<const uint8_t> identity,const State& checkpoint,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},bool repair=false,const DurableResourceTransaction::Policy& resources={}){
        close();
        if(identity.size()<4||identity[0]!='U'||identity[1]!='W'||identity[2]!='T'||identity[3]!='1')
            return RegionWalWrite::InvalidFrame;
        if(lock_.acquire(dir)!=RegionWorldLockResult::Ok)return RegionWalWrite::Busy;
        return recoverLocked(wal,identity,checkpoint,codecs,materialize,validate,repair,resources);
    }
    RegionWalWrite create(const std::filesystem::path& dir,const std::array<uint8_t,16>& world,
        const State& initial,const UnifiedCheckpointSections::Adapters& adapters,size_t budget){
        close();
        std::error_code ec;
        if(!std::filesystem::is_directory(dir,ec)||ec||!std::filesystem::is_empty(dir,ec)||ec)
            return RegionWalWrite::InvalidFrame;
        if(lock_.acquire(dir)!=RegionWorldLockResult::Ok)return RegionWalWrite::Busy;
        // Recheck under lock: only the persistent lock file may exist.
        for(std::filesystem::directory_iterator it(dir,ec),end;!ec&&it!=end;it.increment(ec)){
            if(it->path().filename()!="world-session.lock"){close();return RegionWalWrite::InvalidFrame;}
        }
        if(ec){close();return RegionWalWrite::IoError;}
        auto staged=std::make_unique<State>(initial);
        staged->sequence=0;
        RegionCheckpointManifest m;
        if(UnifiedCheckpointPublisher::publish(dir,*staged,world,1,adapters,budget,m)!=RegionCheckpointPublishResult::Ok){
            close();return RegionWalWrite::NeedsRecovery;
        }
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointPublisher::identity(m,prefix)){close();return RegionWalWrite::NeedsRecovery;}
        auto result=writer_.open(dir/m.walName(),0,false,prefix);
        if(result!=RegionWalWrite::Ok||writer_.lastSequence()!=0){close();return RegionWalWrite::NeedsRecovery;}
        dir_=dir;manifest_=m;state_=std::move(staged);managed_=true;ready_=true;
        return RegionWalWrite::Ok;
    }
    RegionWalWrite openCheckpoint(const std::filesystem::path& dir,const std::array<uint8_t,16>& world,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},bool repair=false,const DurableResourceTransaction::Policy& resources={}){
        close();
        if(lock_.acquire(dir)!=RegionWorldLockResult::Ok)return RegionWalWrite::Busy;
        std::unique_ptr<State> checkpoint;RegionCheckpointManifest m;
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointLoader::load(dir,world,budget,adapters,factory,checkpoint,m,validate)||
           !UnifiedCheckpointPublisher::identity(m,prefix)){close();return RegionWalWrite::NeedsRecovery;}
        auto result=recoverLocked(dir/m.walName(),prefix,*checkpoint,codecs,materialize,validate,repair,resources);
        if(result==RegionWalWrite::Ok){dir_=dir;manifest_=m;managed_=true;}
        return result;
    }
private:
    RegionWalWrite recoverLocked(const std::filesystem::path& wal,std::span<const uint8_t> identity,const State& checkpoint,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate,bool repair,const DurableResourceTransaction::Policy& resources){
        std::ifstream in(wal,std::ios::binary);
        std::vector<uint8_t> actual(identity.size());
        in.read(reinterpret_cast<char*>(actual.data()),actual.size());
        if(!in||!std::equal(actual.begin(),actual.end(),identity.begin())){close();return RegionWalWrite::NeedsRecovery;}
        auto staged=std::make_unique<State>(checkpoint);
        auto scan=RegionWalReader::scan(in,checkpoint.sequence,[&](const RegionWalFrame& f){
            return UnifiedWorldReplay::apply(staged,f,codecs,materialize,validate,resources);
        });
        in.close();
        if(scan.status!=RegionWalScan::End&&!(repair&&scan.status==RegionWalScan::Incomplete)){
            close();return RegionWalWrite::NeedsRecovery;
        }
        auto result=writer_.open(wal,checkpoint.sequence,repair,identity);
        if(result!=RegionWalWrite::Ok||writer_.lastSequence()!=staged->sequence){
            close();return result==RegionWalWrite::Ok?RegionWalWrite::NeedsRecovery:result;
        }
        state_=std::move(staged);ready_=true;return RegionWalWrite::Ok;
    }
public:
    RegionCheckpointPublishResult checkpoint(uint64_t generation,const UnifiedCheckpointSections::Adapters& adapters,size_t budget){
        using R=RegionCheckpointPublishResult;
        if(!ready_||!managed_||ioPending()||generation<=manifest_.generation)return R::Invalid;
        ready_=false;writer_.close();
        RegionCheckpointManifest next;
        auto result=UnifiedCheckpointPublisher::publish(dir_,*state_,manifest_.identity.world,generation,adapters,budget,next);
        if(result!=R::Ok)return result;
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointPublisher::identity(next,prefix)||writer_.open(dir_/next.walName(),state_->sequence,false,prefix)!=RegionWalWrite::Ok||writer_.lastSequence()!=state_->sequence){writer_.close();return R::Ambiguous;}
        manifest_=next;ready_=true;return R::Ok;
    }
    // Finalized barrier only. No external side effects in replay callbacks.
    RegionWalWrite commit(const RegionWalFrame& frame,const DurableComponentCodecs& codecs,
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={}){
        if(!ready_||ioPending())return RegionWalWrite::NeedsRecovery;
        auto staged=std::make_unique<State>(*state_);
        if(!UnifiedWorldReplay::apply(staged,frame,codecs,materialize,validate,resources))return RegionWalWrite::InvalidFrame;
        auto result=writer_.append(frame);
        if(result!=RegionWalWrite::Ok){ready_=false;return result;}
        state_.swap(staged);return RegionWalWrite::Ok;
    }
    // Enable after startup. IO thread receives only frames or encoded
    // checkpoint bytes; all ECS capture/replay and callbacks stay on owner.
    bool enableAsync(size_t bytes){
        if(async_||!managedReady()||!io_.start(bytes))return false;
        async_=true;return true;
    }
    bool asyncEnabled()const{return async_;}
    bool ioPending()const{return pending_!=Pending::None;}
    AsyncPersistenceIo::Receipt ioStatus()const{return io_.status();}
    IoResult queueCommit(RegionWalFrame frame,const DurableComponentCodecs& codecs,
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={},
        const DurableResourceTransaction::Policy& resources={}){
        if(ioPending())return IoResult::Pending;
        if(!async_||!managedReady()||ticket_==UINT64_MAX)return IoResult::Failed;
        auto staged=std::make_unique<State>(*state_);
        if(!UnifiedWorldReplay::apply(staged,frame,codecs,materialize,validate,resources))return IoResult::Failed;
        const size_t bytes=frame.payload.size()+RegionWalCodec::headerSize;
        auto submit=io_.submit(ticket_+1,bytes,[this,frame=std::move(frame)](){
            return writer_.append(frame)==RegionWalWrite::Ok;
        });
        if(submit!=AsyncPersistenceIo::Submit::Accepted)return IoResult::Failed;
        ++ticket_;prepared_=std::move(staged);pending_=Pending::Append;
        return IoResult::Pending;
    }
    IoResult queueCheckpoint(uint64_t generation,const UnifiedCheckpointSections::Adapters& adapters,size_t budget){
        if(ioPending())return IoResult::Pending;
        if(!async_||!managedReady()||generation<=manifest_.generation||ticket_==UINT64_MAX)return IoResult::Failed;
        std::vector<uint8_t> bytes;
        if(!UnifiedCheckpointSections::capture(*state_,manifest_.identity.world,adapters,budget,bytes))return IoResult::Failed;
        RegionCheckpointManifest m;
        m.generation=generation;m.identity.world=manifest_.identity.world;
        m.identity.sequence=state_->sequence;m.identity.revision=state_->ledger.lastRevision();
        m.identity.digest=pack::sha256(bytes.data(),bytes.size());m.checkpointBytes=bytes.size();
        auto submit=io_.submit(ticket_+1,bytes.size(),[this,m,dir=dir_,bytes=std::move(bytes)](){
            writer_.close();
            if(RegionCheckpointPublisher::publish(dir,m,bytes,true)!=RegionCheckpointPublishResult::Ok)return false;
            std::vector<uint8_t> prefix;
            return UnifiedCheckpointPublisher::identity(m,prefix)&&
                writer_.open(dir/m.walName(),m.identity.sequence,false,prefix)==RegionWalWrite::Ok&&
                writer_.lastSequence()==m.identity.sequence;
        });
        if(submit!=AsyncPersistenceIo::Submit::Accepted)return IoResult::Failed;
        ++ticket_;preparedManifest_=m;pending_=Pending::Checkpoint;
        return IoResult::Pending;
    }
    // Owner consumes the ordered receipt. Only here is private ECS state or
    // the new manifest published. A failed/ambiguous write poisons the session.
    IoResult pollIo(){
        if(!ioPending())return ready_?IoResult::Idle:IoResult::Failed;
        AsyncPersistenceIo::Receipt receipt;
        if(!io_.take(receipt))return IoResult::Pending;
        const auto operation=pending_;pending_=Pending::None;
        if(receipt.ticket!=ticket_||receipt.status!=AsyncPersistenceIo::Status::Durable){
            prepared_.reset();ready_=false;return IoResult::Failed;
        }
        if(operation==Pending::Append){
            state_.swap(prepared_);prepared_.reset();return IoResult::Committed;
        }
        manifest_=preparedManifest_;return IoResult::Checkpointed;
    }
    // Managed sessions only; caller serializes these operations at a finalized
    // barrier. Read-only operations do not publish their temporary ECS worlds.
    bool managedReady()const{return ready_&&managed_&&!ioPending()&&lock_.held();}
    const RegionCheckpointManifest* manifest()const{return managedReady()?&manifest_:nullptr;}
    UnifiedBackupCatalogResult backups(size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,const DurableComponentCodecs& codecs,
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={},
        bool verify=false,const DurableResourceTransaction::Policy& resources={})const{
        if(!managedReady())return {};
        return UnifiedBackupCatalog::list(dir_,manifest_,budget,adapters,factory,codecs,materialize,validate,verify,resources);
    }
    RegionRetentionPlan retentionPlan(RegionRetentionPolicy policy,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={})const{
        if(!managedReady())return {};
        return UnifiedCheckpointRetention::plan(dir_,manifest_,policy,budget,adapters,factory,codecs,materialize,validate,resources);
    }
    RegionRetentionCleanup cleanupRetention(RegionRetentionPolicy policy,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={}){
        if(!managedReady())return {};
        return UnifiedCheckpointRetention::cleanup(dir_,manifest_,policy,budget,adapters,factory,codecs,materialize,validate,resources);
    }
    RegionWalWrite forkGeneration(uint64_t generation,const std::filesystem::path& destination,
        const std::array<uint8_t,16>& newWorld,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={})const{
        if(!managedReady())return RegionWalWrite::NeedsRecovery;
        if(!generation||generation>manifest_.generation||newWorld==manifest_.identity.world||
           std::none_of(newWorld.begin(),newWorld.end(),[](auto b){return b!=0;}))
            return RegionWalWrite::InvalidFrame;
        std::unique_ptr<State> recovered;
        if(!UnifiedBackupRead::recover(dir_,generation,manifest_.identity.world,budget,
            adapters,factory,codecs,materialize,recovered,validate,resources))return RegionWalWrite::NeedsRecovery;
        UnifiedWalSession destinationSession;
        return destinationSession.create(destination,newWorld,*recovered,adapters,budget);
    }
    const State* state()const{return ready_&&!ioPending()?state_.get():nullptr;}
    void close(){io_.stop();async_=false;pending_=Pending::None;prepared_.reset();ticket_=0;ready_=false;managed_=false;writer_.close();state_.reset();lock_.close();}
};
}
