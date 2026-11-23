#pragma once
#include "RegionCheckpointManifest.h"
#include "RegionWalCommit.h"
#include <filesystem>
#include <fstream>
namespace aiverse::world {
enum class RegionCheckpointLoadStatus { Ok, IoError, InvalidManifest, WrongWorld, InvalidCheckpoint, InvalidWal };
struct RegionCheckpointLoadResult {
    RegionCheckpointLoadStatus status=RegionCheckpointLoadStatus::IoError;
    RegionCheckpointManifest manifest;
    RegionWalScanResult wal;
};
class RegionCheckpointLoader {
public:
    // Caller holds exclusive world save/recovery barrier. Verify computes
    // the agreed digest over the exact checkpoint bytes. Decode must populate
    // a private ledger and must not publish ECS state or other side effects.
    template<class Verify,class Decode>
    static RegionCheckpointLoadResult load(const std::filesystem::path& dir,
        const std::array<uint8_t,16>& expectedWorld,uint64_t maxCheckpointBytes,
        RegionEntityLedger& ledger,Verify&& verify,Decode&& decode,bool acceptIncomplete=false){
        RegionCheckpointLoadResult r;
        std::ifstream manifest(dir/"manifest.bin",std::ios::binary);
        if(!manifest)return r;
        std::vector<uint8_t> b(RegionCheckpointManifestCodec::size);
        manifest.read(reinterpret_cast<char*>(b.data()),b.size());
        if(!manifest||manifest.peek()!=std::char_traits<char>::eof()||
           manifest.bad()||!RegionCheckpointManifestCodec::decode(b,r.manifest)){
            r.status=RegionCheckpointLoadStatus::InvalidManifest;return r;
        }
        if(r.manifest.identity.world!=expectedWorld){r.status=RegionCheckpointLoadStatus::WrongWorld;return r;}
        const auto n=r.manifest.checkpointBytes;
        if(n>maxCheckpointBytes||n>uint64_t(std::numeric_limits<size_t>::max())||
           n>uint64_t(std::numeric_limits<std::streamsize>::max())){
            r.status=RegionCheckpointLoadStatus::InvalidCheckpoint;return r;
        }
        std::ifstream checkpoint(dir/r.manifest.checkpointName(),std::ios::binary);
        if(!checkpoint)return r;
        b.resize(static_cast<size_t>(n));
        checkpoint.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(n));
        if(!checkpoint||checkpoint.peek()!=std::char_traits<char>::eof()||checkpoint.bad()||
           !verify(std::span<const uint8_t>(b),r.manifest.identity.digest)){
            r.status=RegionCheckpointLoadStatus::InvalidCheckpoint;return r;
        }
        RegionEntityLedger staged;
        if(!decode(std::span<const uint8_t>(b),staged)||staged.lastRevision()!=r.manifest.identity.revision){
            r.status=RegionCheckpointLoadStatus::InvalidCheckpoint;return r;
        }
        std::ifstream wal(dir/r.manifest.walName(),std::ios::binary);
        if(!wal)return r;
        std::vector<uint8_t> walIdentity(RegionCheckpointManifestCodec::size);
        wal.read(reinterpret_cast<char*>(walIdentity.data()),walIdentity.size());
        RegionCheckpointManifest bound;
        if(!wal||!RegionCheckpointManifestCodec::decode(walIdentity,bound)||
           bound.identity!=r.manifest.identity||bound.generation!=r.manifest.generation||
           bound.checkpointBytes!=r.manifest.checkpointBytes){r.status=RegionCheckpointLoadStatus::InvalidWal;return r;}
        r.wal=RegionWalReader::scan(wal,r.manifest.identity.sequence,[&](const RegionWalFrame& frame){
            return RegionWalTransaction::replay(staged,frame);
        });
        if(r.wal.status!=RegionWalScan::End&&!(acceptIncomplete&&r.wal.status==RegionWalScan::Incomplete)){
            r.status=RegionCheckpointLoadStatus::InvalidWal;return r;
        }
        ledger=std::move(staged);r.status=RegionCheckpointLoadStatus::Ok;return r;
    }
};
}
