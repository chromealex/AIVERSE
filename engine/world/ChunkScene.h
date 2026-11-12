#pragma once
#include "ChunkStreamer.h"
#include "engine/data/Json.h"
#include <string>

namespace aiverse::world {
// Sparse presentation chunks, independent of authoritative collision residency.
// Reader uses platform::readContentFile in applications, so packs and loose files share the path.
class ChunkScene {
public:
    using Reader=std::function<bool(const std::string&,std::string&)>;
    void configure(const Json& manifest);
    void update(Vec3 observer,int radius,size_t loadBudget,const Reader& read);
    void update(std::span<const Vec3> observers,int radius,std::span<const Cell> pinned,
                size_t loadBudget,const Reader& read);
    const std::map<Cell,std::vector<Bounds>>& chunks() const {return chunks_;}
    const std::string& error()const{return error_;}
    const StreamStatus& status()const{return streamer_.status();}
    size_t residentCount()const{return chunks_.size();}
    bool enabled()const{return !files_.empty();}
private:
    struct Source { std::string file; size_t maxBoxes=10000; };
    int cellSize_=256;
    bool localCoordinates_=false;
    ChunkStreamer streamer_;
    std::map<Cell,Source> files_;
    std::map<Cell,std::vector<Bounds>> chunks_;
    std::string error_;
};
}
