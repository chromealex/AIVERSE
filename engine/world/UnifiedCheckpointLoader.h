#pragma once
#include "UnifiedCheckpointPublisher.h"
#include <limits>
namespace aiverse::world {
class UnifiedCheckpointLoader {
public:
    // Called on the loading thread; false cancels before publication.
    using ReadProgress=std::function<bool(uint64_t,uint64_t)>;
    // Caller holds world lifetime lock throughout load and WAL replay.
    static bool load(const std::filesystem::path& dir,const std::array<uint8_t,16>& world,
        size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,
        std::unique_ptr<UnifiedWorldReplay::State>& out,RegionCheckpointManifest& manifest,
        const UnifiedWorldReplay::Validator& validate={},const ReadProgress& progress={}){
        std::ifstream in(dir/"manifest.bin",std::ios::binary);
        std::vector<uint8_t> raw(RegionCheckpointManifestCodec::size);
        in.read(reinterpret_cast<char*>(raw.data()),raw.size());
        RegionCheckpointManifest m;
        if(!in||in.peek()!=std::char_traits<char>::eof()||in.bad()||
           !RegionCheckpointManifestCodec::decode(raw,m)||m.identity.world!=world)return false;
        return loadGeneration(dir,m,world,budget,adapters,factory,out,manifest,validate,progress);
    }
    static bool loadGeneration(const std::filesystem::path& dir,const RegionCheckpointManifest& m,
        const std::array<uint8_t,16>& world,size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,std::unique_ptr<UnifiedWorldReplay::State>& out,
        RegionCheckpointManifest& manifest,const UnifiedWorldReplay::Validator& validate={},const ReadProgress& progress={}){
        if(!m.valid()||m.identity.world!=world||m.checkpointBytes>budget||m.checkpointBytes>uint64_t(std::numeric_limits<std::streamsize>::max()))return false;
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointPublisher::identity(m,prefix))return false;
        std::ifstream wal(dir/m.walName(),std::ios::binary);
        std::vector<uint8_t> actual(prefix.size());
        wal.read(reinterpret_cast<char*>(actual.data()),actual.size());
        if(!wal||actual!=prefix)return false;
        std::ifstream cp(dir/m.checkpointName(),std::ios::binary);
        std::vector<uint8_t> bytes(size_t(m.checkpointBytes));
        if(progress&&!progress(0,m.checkpointBytes))return false;
        size_t offset=0;
        while(offset<bytes.size()){
            const size_t count=std::min<size_t>(bytes.size()-offset,1024*1024);
            cp.read(reinterpret_cast<char*>(bytes.data()+offset),std::streamsize(count));
            if(!cp)return false;
            offset+=count;
            if(progress&&!progress(offset,m.checkpointBytes))return false;
        }
        if(cp.peek()!=std::char_traits<char>::eof()||cp.bad()||
           !RegionCheckpointArchive::verify(bytes,m.identity.digest))return false;
        if(progress&&!progress(offset,m.checkpointBytes))return false;
        std::unique_ptr<UnifiedWorldReplay::State> staged;
        if(!UnifiedCheckpointSections::restore(bytes,budget,world,adapters,factory,staged,validate)||
           staged->sequence!=m.identity.sequence||staged->ledger.lastRevision()!=m.identity.revision)return false;
        out.swap(staged);manifest=m;return true;
    }
};
}
