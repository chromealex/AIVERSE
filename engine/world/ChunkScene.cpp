#include "ChunkScene.h"
#include <algorithm>

namespace aiverse::world {
void ChunkScene::configure(const Json& manifest) {
    files_.clear();chunks_.clear();streamer_=Streamer{};error_.clear();
    cellSize_=int(manifest.getInt("cellSize",256));
    if(cellSize_<16||cellSize_>4096)throw JsonError("world.cellSize: expected 16..4096");
    if(!manifest.has("chunks"))return;
    if(!manifest["chunks"].isArray()||manifest["chunks"].size()>100000)throw JsonError("world.chunks: expected at most 100000 entries");
    for(const auto& entry:manifest["chunks"].items()) {
        const auto& cell=entry["cell"];if(!cell.isArray()||cell.size()!=3)throw JsonError("world.chunk.cell: expected three integers");
        int32_t xyz[3];
        for(size_t i=0;i<3;++i){if(!cell[i].isNumber()||cell[i].fx().raw%Fixed::kOne||cell[i].integer()< -100000||cell[i].integer()>100000)throw JsonError("world.chunk.cell: invalid coordinate");xyz[i]=int32_t(cell[i].integer());}
        auto file=entry.getStr("file");
        if(file.empty()||file.front()=='/'||file.find("..")!=std::string::npos||file.find('\\')!=std::string::npos||file.find(':')!=std::string::npos)throw JsonError("world.chunk.file: expected project-relative path");
        if(!files_.emplace(Cell{xyz[0],xyz[1],xyz[2]},file).second)throw JsonError("world.chunk: duplicate cell");
    }
}
void ChunkScene::update(Vec3 observer,int radius,size_t loadBudget,const Reader& read) {
    radius=std::clamp(radius,0,4);auto center=SpatialWorld(cellSize_).cell(observer);std::set<Cell> desired;
    // Enumerate only nearby cells, not the entire manifest.
    for(int x=-radius;x<=radius;++x)for(int y=-radius;y<=radius;++y)for(int z=-radius;z<=radius;++z) {
        Cell cell{center.x+x,center.y+y,center.z+z};if(files_.contains(cell))desired.insert(cell);
    }
    streamer_.desire(std::move(desired));
    streamer_.pump(loadBudget,[&](Cell cell) {
        try {
            std::string bytes;
            if(!read(files_.at(cell),bytes)||bytes.size()>4*1024*1024){error_="world.chunk: read failed or exceeds 4 MiB";return false;}
            auto data=Json::parse(bytes);const auto& boxes=data["boxes"];
            if(!boxes.isArray()||boxes.size()>10000)throw JsonError("world.chunk.boxes: expected at most 10000 boxes");
            std::vector<Bounds> result;
            for(const auto& b:boxes.items()) {
                auto readVec=[](const Json& j){if(!j.isArray()||j.size()!=3)throw JsonError("world.chunk.box: invalid vector");Vec3 v;Fixed* xyz[]={&v.x,&v.y,&v.z};for(size_t i=0;i<3;++i){if(!j[i].isNumber()||fxAbs(j[i].fx())>100000_fx)throw JsonError("world.chunk.box: invalid coordinate");*xyz[i]=j[i].fx();}return v;};
                Bounds box{readVec(b["min"]),readVec(b["max"])};
                if(box.min.x>=box.max.x||box.min.y>=box.max.y||box.min.z>=box.max.z)throw JsonError("world.chunk.box: invalid bounds");
                result.push_back(box);
            }
            chunks_[cell]=std::move(result);error_.clear();return true;
        }catch(const std::exception& e){error_=e.what();return false;}
    },[&](Cell cell){chunks_.erase(cell);});
}
}
