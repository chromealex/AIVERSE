#pragma once
#include "UnifiedWorldReplay.h"
#include "RegionWalWriter.h"
#include "RegionWorldSessionLock.h"
namespace aiverse::world {
class UnifiedWalSession {
    RegionWorldSessionLock lock_;
    RegionWalWriter writer_;
    std::unique_ptr<UnifiedWorldReplay::State> state_;
    bool ready_=false;
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
    void close(){ready_=false;writer_.close();state_.reset();lock_.close();}
};
}
