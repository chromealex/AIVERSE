#include "ChunkScene.h"
#include <algorithm>
#include <limits>

namespace aiverse::world {
namespace {
int64_t integer(const Json& object,const char* key,int64_t fallback,int64_t low,int64_t high) {
    if(!object.has(key))return fallback;
    const auto& value=object[key];
    if(!value.isNumber()||value.fx().raw%Fixed::kOne||value.integer()<low||value.integer()>high)
        throw JsonError(std::string("world.")+key+": invalid integer or out of range");
    return value.integer();
}
Cell observerCell(Vec3 point,int size) {
    const int64_t rawSize=int64_t(size)*Fixed::kOne;
    auto floor=[&](Fixed f){auto n=f.raw/rawSize;if(f.raw%rawSize<0)--n;
        return int32_t(std::clamp(n,int64_t(std::numeric_limits<int32_t>::min()),int64_t(std::numeric_limits<int32_t>::max())));};
    return {floor(point.x),floor(point.y),floor(point.z)};
}
}
void ChunkScene::configure(const Json& manifest) {
    // Validate off to the side so a malformed hot reload preserves the running scene.
    if(!manifest.isNull()&&!manifest.isObject())throw JsonError("world: expected object");
    ChunkScene next;
    next.cellSize_=int(integer(manifest,"cellSize",256,16,4096));
    if(manifest.has("coordinates")&&!manifest["coordinates"].isString())throw JsonError("world.coordinates: expected string");
    const auto coordinates=manifest.getStr("coordinates","absolute");
    if(coordinates!="absolute"&&coordinates!="local")throw JsonError("world.coordinates: expected absolute or local");
    next.localCoordinates_=coordinates=="local";
    StreamLimits limits;
    limits.residentBytes=size_t(integer(manifest,"maxResidentBytes",64*1024*1024,1,1024*1024*1024));
    limits.residentChunks=size_t(integer(manifest,"maxResidentChunks",512,1,100000));
    limits.retryUpdates=uint32_t(integer(manifest,"retryUpdates",30,1,3600));
    next.streamer_=ChunkStreamer(limits);
    if(manifest.has("chunks")) {
        if(!manifest["chunks"].isArray()||manifest["chunks"].size()>100000)throw JsonError("world.chunks: expected at most 100000 entries");
        for(const auto& entry:manifest["chunks"].items()) {
            const auto& cell=entry["cell"];if(!cell.isArray()||cell.size()!=3)throw JsonError("world.chunk.cell: expected three integers");
            int32_t xyz[3];
            for(size_t i=0;i<3;++i){if(!cell[i].isNumber()||cell[i].fx().raw%Fixed::kOne||cell[i].integer()< -100000||cell[i].integer()>100000)throw JsonError("world.chunk.cell: invalid coordinate");xyz[i]=int32_t(cell[i].integer());}
            auto file=entry.getStr("file");
            if(file.empty()||file.front()=='/'||file.find("..")!=std::string::npos||file.find('\\')!=std::string::npos||file.find(':')!=std::string::npos)throw JsonError("world.chunk.file: expected project-relative path");
            const Cell key{xyz[0],xyz[1],xyz[2]};
            const size_t maxBoxes=size_t(integer(entry,"maxBoxes",10000,1,10000));
            if(!next.files_.emplace(key,Source{file,maxBoxes}).second)throw JsonError("world.chunk: duplicate cell");
            next.streamer_.add(key,maxBoxes*sizeof(Bounds));
        }
    }
    *this=std::move(next);
}
void ChunkScene::update(Vec3 observer,int radius,size_t loadBudget,const Reader& read) {
    update(std::span<const Vec3>(&observer,1),radius,{},loadBudget,read);
}
void ChunkScene::update(std::span<const Vec3> observers,int radius,std::span<const Cell> pinned,
                        size_t loadBudget,const Reader& read) {
    std::vector<StreamObserver> interests;interests.reserve(observers.size());
    for(auto observer:observers)interests.push_back({observerCell(observer,cellSize_),radius});
    error_.clear();
    streamer_.update(interests,pinned,loadBudget,[&](Cell cell) {
        try {
            const auto& source=files_.at(cell);
            std::string bytes;
            if(!read(source.file,bytes)||bytes.size()>4*1024*1024){error_="world.chunk: read failed or exceeds 4 MiB: "+source.file;return false;}
            auto data=Json::parse(bytes);const auto& boxes=data["boxes"];
            if(!boxes.isArray()||boxes.size()>source.maxBoxes)throw JsonError("world.chunk.boxes: exceeds declared maxBoxes: "+source.file);
            std::vector<Bounds> result;result.reserve(boxes.size());
            const auto offset=localCoordinates_?Vec3{Fixed::fromInt(int64_t(cell.x)*cellSize_),Fixed::fromInt(int64_t(cell.y)*cellSize_),Fixed::fromInt(int64_t(cell.z)*cellSize_)}:Vec3{};
            for(const auto& b:boxes.items()) {
                auto readVec=[](const Json& j){if(!j.isArray()||j.size()!=3)throw JsonError("world.chunk.box: invalid vector");Vec3 v;Fixed* xyz[]={&v.x,&v.y,&v.z};for(size_t i=0;i<3;++i){if(!j[i].isNumber()||j[i].fx()< -100000_fx||j[i].fx()>100000_fx)throw JsonError("world.chunk.box: invalid coordinate");*xyz[i]=j[i].fx();}return v;};
                Bounds box{readVec(b["min"])+offset,readVec(b["max"])+offset};
                if(box.min.x>=box.max.x||box.min.y>=box.max.y||box.min.z>=box.max.z)throw JsonError("world.chunk.box: invalid bounds");
                result.push_back(box);
            }
            chunks_[cell]=std::move(result);return true;
        }catch(const std::exception& e){error_=e.what();return false;}
    },[&](Cell cell){chunks_.erase(cell);});
    if(error_.empty()&&status().failed)error_="world.chunk: load failed; waiting to retry";
}
}
