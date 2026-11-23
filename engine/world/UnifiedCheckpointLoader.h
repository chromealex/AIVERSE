#pragma once
#include "UnifiedCheckpointPublisher.h"
#include <limits>
namespace aiverse::world {
class UnifiedCheckpointLoader {
public:
    // Caller holds world lifetime lock throughout load and WAL replay.
    static bool load(const std::filesystem::path& dir,const std::array<uint8_t,16>& world,
        size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,
        std::unique_ptr<UnifiedWorldReplay::State>& out,RegionCheckpointManifest& manifest,
        const UnifiedWorldReplay::Validator& validate={}){
        std::ifstream in(dir/"manifest.bin",std::ios::binary);
        std::vector<uint8_t> raw(RegionCheckpointManifestCodec::size);
        in.read(reinterpret_cast<char*>(raw.data()),raw.size());
        RegionCheckpointManifest m;
        if(!in||in.peek()!=std::char_traits<char>::eof()||in.bad()||
           !RegionCheckpointManifestCodec::decode(raw,m)||m.identity.world!=world||
           m.checkpointBytes>budget||m.checkpointBytes>uint64_t(std::numeric_limits<std::streamsize>::max()))return false;
        std::vector<uint8_t> prefix;
        if(!UnifiedCheckpointPublisher::identity(m,prefix))return false;
        std::ifstream wal(dir/m.walName(),std::ios::binary);
        std::vector<uint8_t> actual(prefix.size());
        wal.read(reinterpret_cast<char*>(actual.data()),actual.size());
        if(!wal||actual!=prefix)return false;
        std::ifstream cp(dir/m.checkpointName(),std::ios::binary);
        std::vector<uint8_t> bytes(size_t(m.checkpointBytes));
        cp.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!cp||cp.peek()!=std::char_traits<char>::eof()||cp.bad()||
           !RegionCheckpointArchive::verify(bytes,m.identity.digest))return false;
        std::unique_ptr<UnifiedWorldReplay::State> staged;
        if(!UnifiedCheckpointSections::restore(bytes,budget,world,adapters,factory,staged,validate)||
           staged->sequence!=m.identity.sequence||staged->ledger.lastRevision()!=m.identity.revision)return false;
        out.swap(staged);manifest=m;return true;
    }
};
}
