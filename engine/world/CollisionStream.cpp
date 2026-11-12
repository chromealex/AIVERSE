#include "CollisionStream.h"
#include <algorithm>

namespace aiverse::world {
namespace {
bool seams(Cell cell,const TerrainTile& tile,const std::map<Cell,TerrainTile>& neighbors) {
    if(!tile.resolution)return true;
    for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}) {
        auto it=neighbors.find({cell.x+dx,0,cell.z+dz});if(it==neighbors.end()||!it->second.resolution)continue;
        const auto& other=it->second;const int n=tile.resolution;
        if(n!=other.resolution)return false;
        for(int i=0;i<n;++i){int a=dx<0?i*n:dx>0?i*n+n-1:dz<0?i:(n-1)*n+i;
            int b=dx<0?i*n+n-1:dx>0?i*n:dz<0?(n-1)*n+i:i;
            if(tile.heights[size_t(a)]!=other.heights[size_t(b)])return false;}
    }
    return true;
}
}
CollisionStream::CollisionStream(const Json& manifest,AssetReader reader,SpatialWorld& target):assets_(WorldAssets::parse(manifest)),target_(target) {
    for(const auto& [id,c]:target.colliders()){(void)c;if(id>=0x80000000u)throw JsonError("world: static collider IDs must be below 2147483648");}
    auto verified=assets_.verifiedReader(reader);
    stream_=std::make_unique<AsyncChunkStream>(assets_.limits,assets_.stagingBytes,assets_.maxJobs,[this,verified](Cell cell){
        const auto& asset=assets_.chunks.at(cell);std::string bytes;
        if(!verified(asset.file,bytes))throw JsonError("world.collision: read/hash failure: "+asset.file);
        auto result=std::make_shared<Prepared>();result->collision=readChunkCollision(Json::parse(bytes),assets_.cellSize);
        // Conservative allowance for shape/index nodes plus CPU data; no GPU allocation here.
        size_t cost=sizeof(Prepared)+result->collision.terrain.heights.capacity()*sizeof(Fixed);
        const auto origin=assets_.origin(cell);
        auto account=[&](Bounds b){b.min+=origin;b.max+=origin;auto lo=target_.cell(b.min),hi=target_.cell(b.max);
            int64_t nx=int64_t(hi.x)-lo.x+1,ny=int64_t(hi.y)-lo.y+1,nz=int64_t(hi.z)-lo.z+1;
            size_t cells=(nx>4096||ny>4096||nz>4096||nx*ny*nz>4096)?1:size_t(nx*ny*nz);
            cost+=512+cells*256;if(cost>asset.bytes)throw JsonError("world.collision: reservedBytes too small");};
        for(auto box:result->collision.boxes)account(box);
        for(auto t:result->collision.triangles)account({{fxMin(t.a.x,fxMin(t.b.x,t.c.x)),fxMin(t.a.y,fxMin(t.b.y,t.c.y)),fxMin(t.a.z,fxMin(t.b.z,t.c.z))},{fxMax(t.a.x,fxMax(t.b.x,t.c.x)),fxMax(t.a.y,fxMax(t.b.y,t.c.y)),fxMax(t.a.z,fxMax(t.b.z,t.c.z))}});
        return result;
    });
    for(const auto& [cell,asset]:assets_.chunks)if(!stream_->add(cell,asset.bytes))throw JsonError("world: chunk exceeds staging budget");
}
CollisionStream::~CollisionStream(){stream_->shutdown([this](Cell c){retire(c);});}
void CollisionStream::retire(Cell cell){auto it=ids_.find(cell);if(it!=ids_.end()){for(auto id:it->second)target_.remove(id);ids_.erase(it);}terrain_.erase(cell);}
bool CollisionStream::publish(Cell cell,const AsyncChunkStream::Payload& payload) {
    const auto& collision=static_cast<const Prepared&>(*payload).collision;
    if(!seams(cell,collision.terrain,terrain_))throw JsonError("world.terrain: neighbor resolution or edge heights differ");
    auto offset=assets_.origin(cell);auto id=assets_.chunks.at(cell).idBase;auto& ids=ids_[cell];ids.reserve(collision.boxes.size()+collision.triangles.size());
    for(auto box:collision.boxes){++id;box.min+=offset;box.max+=offset;if(target_.colliders().contains(id)||!target_.add({id,box})){retire(cell);return false;}ids.push_back(id);}
    for(auto triangle:collision.triangles){++id;triangle.a+=offset;triangle.b+=offset;triangle.c+=offset;if(target_.colliders().contains(id)||!target_.addTriangle(id,triangle)){retire(cell);return false;}ids.push_back(id);}
    terrain_[cell]=collision.terrain;return true;
}
bool CollisionStream::prepareRegions(std::span<const Bounds> regions,size_t requests,size_t publications) {
    auto pins=assets_.intersecting(regions);
    auto status=stream_->update({},pins,requests,publications,[this](Cell c,const auto& p){return publish(c,p);},[this](Cell c){retire(c);});
    return status.complete();
}
bool CollisionStream::covers(Bounds region)const {
    auto cells=assets_.intersecting({&region,1});for(auto c:cells)if(!stream_->ready(c))return false;return true;
}
}
