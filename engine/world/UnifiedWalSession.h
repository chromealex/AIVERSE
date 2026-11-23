#pragma once
#include "UnifiedWorldReplay.h"
#include "UnifiedCheckpointLoader.h"
#include "RegionWalWriter.h"
#include "RegionWorldSessionLock.h"
namespace aiverse::world {
class UnifiedWalSession {
    RegionWorldSessionLock lock_;
    RegionWalWriter writer_;
    std::unique_ptr<UnifiedWorldReplay::State> state_;
    bool ready_=false;
    std::filesystem::path dir_;
    RegionCheckpointManifest manifest_;
    bool managed_=false;
public:
    using State=UnifiedWorldReplay::State;
    // Caller supplies a verified checkpoint and its exact nonempty identity
    // prefix, including a unified-format discriminator. Existing WAL only.
    RegionWalWrite open(const std::filesystem::path& dir,const std::filesystem::path& wal,
        std::span<const uint8_t> identity,const State& checkpoint,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},bool repair=false){
        close();
        if(identity.size()<4||identity[0]!='U'||identity[1]!='W'||identity[2]!='T'||identity[3]!='1')
            return RegionWalWrite::InvalidFrame;
        if(lock_.acquire(dir)!=RegionWorldLockResult::Ok)return RegionWalWrite::Busy;
        return recoverLocked(wal,identity,checkpoint,codecs,materialize,validate,repair);
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
        const UnifiedWorldReplay::Validator& validate={},bool repair=false){
        close();
        if(lock_.acquire(dir)!=RegionWorldLockResult::Ok)return RegionWalWrite::Busy;
        std::unique_ptr<State> checkpoint;RegionCheckpointManifest m;
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointLoader::load(dir,world,budget,adapters,factory,checkpoint,m,validate)||
           !UnifiedCheckpointPublisher::identity(m,prefix)){close();return RegionWalWrite::NeedsRecovery;}
        auto result=recoverLocked(dir/m.walName(),prefix,*checkpoint,codecs,materialize,validate,repair);
        if(result==RegionWalWrite::Ok){dir_=dir;manifest_=m;managed_=true;}
        return result;
    }
private:
    RegionWalWrite recoverLocked(const std::filesystem::path& wal,std::span<const uint8_t> identity,const State& checkpoint,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate,bool repair){
        std::ifstream in(wal,std::ios::binary);
        std::vector<uint8_t> actual(identity.size());
        in.read(reinterpret_cast<char*>(actual.data()),actual.size());
        if(!in||!std::equal(actual.begin(),actual.end(),identity.begin())){close();return RegionWalWrite::NeedsRecovery;}
        auto staged=std::make_unique<State>(checkpoint);
        auto scan=RegionWalReader::scan(in,checkpoint.sequence,[&](const RegionWalFrame& f){
            return UnifiedWorldReplay::apply(staged,f,codecs,materialize,validate);
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
        if(!ready_||!managed_||generation<=manifest_.generation)return R::Invalid;
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
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={}){
        if(!ready_)return RegionWalWrite::NeedsRecovery;
        auto staged=std::make_unique<State>(*state_);
        if(!UnifiedWorldReplay::apply(staged,frame,codecs,materialize,validate))return RegionWalWrite::InvalidFrame;
        auto result=writer_.append(frame);
        if(result!=RegionWalWrite::Ok){ready_=false;return result;}
        state_.swap(staged);return RegionWalWrite::Ok;
    }
    const State* state()const{return ready_?state_.get():nullptr;}
    void close(){ready_=false;managed_=false;writer_.close();state_.reset();lock_.close();}
};
}
