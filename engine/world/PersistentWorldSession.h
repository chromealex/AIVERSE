#pragma once
#include "RegionWorldSessionLock.h"
#include "RegionCheckpointArchive.h"
#include "RegionWalFinality.h"
namespace aiverse::world {
enum class PersistentWorldResult { Ok, Busy, Invalid, IoError, NeedsRecovery, Unsupported };
class PersistentWorldSession {
    RegionWorldSessionLock lock_;
    RegionWalFinality wal_;
    RegionEntityLedger ledger_;
    RegionCheckpointManifest manifest_;
    std::filesystem::path dir_;
    bool ready_=false;
public:
    PersistentWorldSession()=default;
    PersistentWorldSession(const PersistentWorldSession&)=delete;
    PersistentWorldSession& operator=(const PersistentWorldSession&)=delete;
    ~PersistentWorldSession(){close();}
    bool ready()const{return ready_&&wal_.ready();}
    const RegionEntityLedger& ledger()const{return ledger_;}
    const RegionCheckpointManifest& manifest()const{return manifest_;}
    uint64_t sequence()const{return wal_.sequence();}
    PersistentWorldResult open(const std::filesystem::path& dir,
        const std::array<uint8_t,16>& world,uint64_t maxBytes,bool repair=false){
        if(lock_.held())return PersistentWorldResult::Busy;
        auto result=acquire(dir);
        if(result!=PersistentWorldResult::Ok)return result;
        try {
            RegionEntityLedger staged;
            auto loaded=RegionCheckpointArchive::load(dir_,world,maxBytes,staged,repair);
            if(loaded.status!=RegionCheckpointLoadStatus::Ok){close();return PersistentWorldResult::NeedsRecovery;}
            auto opened=wal_.openFinalized(dir_/loaded.manifest.walName(),
                loaded.manifest.identity.sequence,loaded.wal.lastSequence,staged,repair);
            if(opened!=RegionWalWrite::Ok){close();return PersistentWorldResult::NeedsRecovery;}
            ledger_=std::move(staged);manifest_=loaded.manifest;ready_=true;
            return PersistentWorldResult::Ok;
        }catch(...){close();throw;}
    }
    PersistentWorldResult create(const std::filesystem::path& dir,
        const std::array<uint8_t,16>& world,const RegionEntityLedger& initial){
        if(lock_.held())return PersistentWorldResult::Busy;
        auto result=acquire(dir);
        if(result!=PersistentWorldResult::Ok)return result;
        try {
            std::error_code ec;
            if(std::filesystem::exists(dir_/"manifest.bin",ec)||ec){close();return PersistentWorldResult::Invalid;}
            ledger_=initial;manifest_={};manifest_.identity.world=world;manifest_.generation=1;
            result=publish(manifest_);
            if(result!=PersistentWorldResult::Ok)close();
            return result;
        }catch(...){close();throw;}
    }
    // Only finalized authoritative changes. Callback must use tx exclusively,
    // return false on refusal, and never reenter the session or publish side effects.
    template<class Mutate>
    PersistentWorldResult transactFinalized(Mutate&& mutate){
        if(!ready())return PersistentWorldResult::NeedsRecovery;
        RegionEntityLedger::Transaction tx(ledger_,true);
        if(!mutate(tx))return PersistentWorldResult::Invalid;
        try {
            if(wal_.commitFinalized(tx)!=RegionWalWrite::Ok){
                ready_=false;return PersistentWorldResult::NeedsRecovery;
            }
        }catch(...){ready_=false;throw;}
        return PersistentWorldResult::Ok;
    }
    // Caller supplies unused increasing generation; orphan files are not overwritten.
    PersistentWorldResult checkpoint(uint64_t generation){
        if(!ready())return PersistentWorldResult::NeedsRecovery;
        if(generation<=manifest_.generation)return PersistentWorldResult::Invalid;
        auto next=manifest_;next.generation=generation;next.identity.sequence=wal_.sequence();
        ready_=false;wal_.close();
        return publish(next);
    }
    void close()noexcept{ready_=false;wal_.close();lock_.close();}
private:
    PersistentWorldResult acquire(const std::filesystem::path& dir){
        auto r=lock_.acquire(dir);
        if(r==RegionWorldLockResult::Busy)return PersistentWorldResult::Busy;
        if(r==RegionWorldLockResult::Unsupported)return PersistentWorldResult::Unsupported;
        if(r!=RegionWorldLockResult::Ok)return PersistentWorldResult::IoError;
        try{dir_=dir;}catch(...){lock_.close();throw;}
        return PersistentWorldResult::Ok;
    }
    PersistentWorldResult publish(RegionCheckpointManifest next){
        std::vector<uint8_t> bytes;
        if(!RegionCheckpointArchive::encode(ledger_,bytes))return PersistentWorldResult::Invalid;
        next.identity.revision=ledger_.lastRevision();
        next.identity.digest=pack::sha256(bytes.data(),bytes.size());
        next.checkpointBytes=bytes.size();
        auto r=RegionCheckpointPublisher::publish(dir_,next,bytes);
        if(r!=RegionCheckpointPublishResult::Ok)return PersistentWorldResult::NeedsRecovery;
        if(wal_.openFinalized(dir_/next.walName(),next.identity.sequence,
            next.identity.sequence,ledger_)!=RegionWalWrite::Ok)return PersistentWorldResult::NeedsRecovery;
        manifest_=next;ready_=true;return PersistentWorldResult::Ok;
    }
};
}
