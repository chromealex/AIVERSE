#include "WorldAssets.h"
#include "engine/pack/Sha256.h"
#include <algorithm>
#include <cctype>
#include <limits>

namespace aiverse::world {
bool safeAssetPath(const std::string& s){return !s.empty()&&s.front()!='/'&&s.find("..") == std::string::npos&&s.find('\\')==std::string::npos&&s.find(':')==std::string::npos&&s.find('\0')==std::string::npos;}
int64_t worldInteger(const Json& j,const char* key,int64_t fallback,int64_t low,int64_t high) {
    if(!j.has(key))return fallback;const auto& v=j[key];
    if(!v.isNumber()||v.fx().raw%Fixed::kOne||v.integer()<low||v.integer()>high)throw JsonError(std::string("world.")+key+": invalid integer");
    return v.integer();
}
Vec3 worldVector(const Json& j,Fixed limit) {
    if(!j.isArray()||j.size()!=3)throw JsonError("world: expected vector");Vec3 v;Fixed* a[]={&v.x,&v.y,&v.z};
    for(size_t i=0;i<3;++i){if(!j[i].isNumber()||j[i].fx()< -limit||j[i].fx()>limit)throw JsonError("world: coordinate out of range");*a[i]=j[i].fx();}return v;
}
WorldAssets WorldAssets::parse(const Json& j) {
    if(!j.isObject()||!j.getBool("async"))throw JsonError("world.async: expected true");
    worldInteger(j,"version",1,1,1);
    WorldAssets out;out.cellSize=int(worldInteger(j,"cellSize",256,16,4096));
    out.limits.residentBytes=size_t(worldInteger(j,"maxResidentBytes",64*1024*1024,1024,1024*1024*1024));
    out.limits.residentChunks=size_t(worldInteger(j,"maxResidentChunks",128,1,32768));
    out.limits.retryUpdates=uint32_t(worldInteger(j,"retryUpdates",30,1,3600));
    out.stagingBytes=size_t(worldInteger(j,"maxStagingBytes",32*1024*1024,1024,1024*1024*1024));
    out.maxJobs=size_t(worldInteger(j,"maxJobs",4,1,16));
    if(!j["assets"].isObject()||j["assets"].size()>100000)throw JsonError("world.assets: expected path to SHA-256 map");
    for(const auto& [file,value]:j["assets"].members()) {
        const auto hash=value.str();if(!safeAssetPath(file)||hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw JsonError("world.assets: invalid path or SHA-256");
        if(!out.hashes.emplace(file,hash).second)throw JsonError("world.assets: duplicate file");
    }
    if(!j["chunks"].isArray()||j["chunks"].size()>32768)throw JsonError("world.chunks: expected at most 32768 entries");
    for(const auto& entry:j["chunks"].items()) {
        auto p=worldVector(entry["cell"],100000_fx);
        if(p.y.raw||p.x.raw%Fixed::kOne||p.z.raw%Fixed::kOne)throw JsonError("world.cell: expected integer X/Z, Y=0");
        ChunkAsset a;a.cell={int32_t(p.x.toInt()),0,int32_t(p.z.toInt())};a.file=entry.getStr("file");
        a.bytes=size_t(worldInteger(entry,"reservedBytes",0,1024,int64_t(std::min(out.limits.residentBytes,out.stagingBytes))));
        if(!a.bytes||!out.hashes.contains(a.file))throw JsonError("world.chunk: reservedBytes and hashed file required");
        if(!out.chunks.emplace(a.cell,a).second)throw JsonError("world.chunk: duplicate cell");
    }
    uint32_t index=0;for(auto& [cell,a]:out.chunks){(void)cell;a.idBase=0x80000000u+index++*65536u;}
    return out;
}
AssetReader WorldAssets::verifiedReader(const AssetReader& reader)const {
    auto hashes=std::make_shared<const std::map<std::string,std::string>>(this->hashes);
    return [hashes,reader](const std::string& file,std::string& bytes){
        const auto it=hashes->find(file);if(it==hashes->end())return false;
        if(!reader(file,bytes)||bytes.size()>32*1024*1024)return false;
        auto hash=pack::sha256(bytes.data(),bytes.size());return pack::toHex(hash.data(),hash.size())==it->second;
    };
}
Cell WorldAssets::cell(Vec3 p)const {
    auto f=[&](Fixed x){const int64_t size=int64_t(cellSize)*Fixed::kOne;auto q=x.raw/size;if(x.raw%size<0)--q;return int32_t(std::clamp(q,int64_t(INT32_MIN),int64_t(INT32_MAX)));};
    return {f(p.x),0,f(p.z)};
}
std::vector<Cell> WorldAssets::intersecting(std::span<const Bounds> regions)const {
    std::set<Cell> result;
    for(auto region:regions) {
        auto lo=cell(region.min),hi=cell(region.max);
        // Range scan only populated X columns; large weapon ranges never enumerate empty space.
        for(auto it=chunks.lower_bound({lo.x,0,INT32_MIN});it!=chunks.end()&&it->first.x<=hi.x;++it)
            if(it->first.z>=lo.z&&it->first.z<=hi.z)result.insert(it->first);
    }
    return {result.begin(),result.end()};
}
ChunkCollision readChunkCollision(const Json& j,int size) {
    ChunkCollision out;const auto cellSize=Fixed::fromInt(size);
    auto vector=[&](const Json& v){auto p=worldVector(v,100000_fx);if(p.x<0_fx||p.x>cellSize||p.z<0_fx||p.z>cellSize)throw JsonError("world.collision: outside owning X/Z cell");return p;};
    if(j.has("boxes")) {
        if(!j["boxes"].isArray()||j["boxes"].size()>4096)throw JsonError("world.boxes: expected at most 4096 boxes");
        for(const auto& b:j["boxes"].items()){Bounds box{vector(b["min"]),vector(b["max"])};if(box.min.x>=box.max.x||box.min.y>=box.max.y||box.min.z>=box.max.z)throw JsonError("world.box: invalid bounds");out.boxes.push_back(box);}
    }
    if(j.has("collision")) {
        const auto& c=j["collision"];const auto& vertices=c["vertices"];const auto& indices=c["indices"];
        if(!vertices.isArray()||vertices.size()>65536||!indices.isArray()||indices.size()%3||indices.size()>98304)throw JsonError("world.collision: invalid triangle mesh size");
        std::vector<Vec3> points;points.reserve(vertices.size());for(const auto& v:vertices.items())points.push_back(vector(v));
        for(size_t i=0;i<indices.size();i+=3){Vec3 v[3];for(int n=0;n<3;++n){const auto& k=indices[i+size_t(n)];if(!k.isNumber()||k.fx().raw%Fixed::kOne||k.integer()<0||uint64_t(k.integer())>=points.size())throw JsonError("world.collision: invalid index");v[n]=points[size_t(k.integer())];}out.triangles.push_back({v[0],v[1],v[2]});}
    }
    if(j.has("terrain")) {
        const auto& terrain=j["terrain"];auto& t=out.terrain;t.resolution=int(worldInteger(terrain,"resolution",0,2,129));
        const auto& heights=terrain["heights"];
        if(t.resolution<2||!heights.isArray()||heights.size()!=size_t(t.resolution*t.resolution))throw JsonError("world.terrain: expected resolution squared heights");
        for(const auto& h:heights.items()){if(!h.isNumber()||h.fx()< -10000_fx||h.fx()>10000_fx)throw JsonError("world.terrain: invalid height");t.heights.push_back(h.fx());}
        auto point=[&](int x,int z){return Vec3{Fixed::fromRatio(int64_t(x)*size,t.resolution-1),t.heights[size_t(z*t.resolution+x)],Fixed::fromRatio(int64_t(z)*size,t.resolution-1)};};
        for(int z=0;z<t.resolution-1;++z)for(int x=0;x<t.resolution-1;++x){auto a=point(x,z),b=point(x+1,z),c=point(x+1,z+1),d=point(x,z+1);out.triangles.push_back({a,c,b});out.triangles.push_back({a,d,c});}
    }
    if(out.boxes.size()+out.triangles.size()>65535)throw JsonError("world.collision: exceeds per-cell shape limit");
    return out;
}
}
