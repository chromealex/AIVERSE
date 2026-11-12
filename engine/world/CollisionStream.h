#pragma once
#include "WorldAssets.h"

namespace aiverse::world {
class CollisionStream {
public:
    CollisionStream(const Json& manifest,AssetReader reader,SpatialWorld& target);
    ~CollisionStream();
    // Must be called before advancing a simulation tick. False means wait, never simulate holes.
    bool prepareRegions(std::span<const Bounds> regions,size_t requests=4,size_t publications=4);
    bool covers(Bounds region)const;
    const AsyncChunkStream::Status& status()const{return stream_->status();}
    const WorldAssets& assets()const{return assets_;}
private:
    struct Prepared final:AsyncChunkStream::Resource { ChunkCollision collision; };
    WorldAssets assets_;
    SpatialWorld& target_;
    std::map<Cell,std::vector<uint32_t>> ids_;
    std::map<Cell,TerrainTile> terrain_;
    std::unique_ptr<AsyncChunkStream> stream_;
    bool publish(Cell,const AsyncChunkStream::Payload&);
    void retire(Cell);
};
}
