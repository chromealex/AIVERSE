#pragma once
#include "AsyncChunkStream.h"
#include "engine/data/Json.h"

namespace aiverse::world {
using AssetReader=std::function<bool(const std::string&,std::string&)>;
struct ChunkAsset { Cell cell;std::string file;size_t bytes=0;uint32_t idBase=0; };
struct WorldAssets {
    int cellSize=256;
    StreamLimits limits;
    size_t stagingBytes=32*1024*1024,maxJobs=4;
    std::map<Cell,ChunkAsset> chunks;
    std::map<std::string,std::string> hashes;
    static WorldAssets parse(const Json&);
    AssetReader verifiedReader(const AssetReader&)const;
    // Ground partitions are X/Z columns: their Y range is not tied to camera altitude.
    Cell cell(Vec3)const;
    Vec3 origin(Cell c)const{return {Fixed::fromInt(int64_t(c.x)*cellSize),{},Fixed::fromInt(int64_t(c.z)*cellSize)};}
    std::vector<Cell> intersecting(std::span<const Bounds>)const;
};
struct TerrainTile { int resolution=0;std::vector<Fixed> heights; };
struct ChunkCollision {
    std::vector<Bounds> boxes;
    std::vector<Triangle> triangles;
    TerrainTile terrain;
};
// Local-space authored collision triangles; terrain uses the same diagonal as the render mesh.
ChunkCollision readChunkCollision(const Json&,int cellSize);
bool safeAssetPath(const std::string&);
int64_t worldInteger(const Json&,const char*,int64_t,int64_t,int64_t);
Vec3 worldVector(const Json&,Fixed limit);
}
