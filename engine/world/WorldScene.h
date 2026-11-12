#pragma once
#include "WorldAssets.h"
#include "engine/render/Renderer3D.h"

namespace aiverse::world {
// CPU model decode/terrain tessellation in the worker; GPU publication on update's thread.
class WorldScene {
public:
    WorldScene(const Json& manifest,AssetReader reader);
    ~WorldScene();
    void update(gfx::Device&,std::span<const Vec3> observers,int radius=2);
    void draw(render::Renderer3D&,Cell renderOrigin,int originCellSize);
    void shutdown(gfx::Device&);
    const AsyncChunkStream::Status& status()const{return stream_->status();}
private:
    struct Instance { size_t model=0;render::mat4 transform; };
    struct Prepared final:AsyncChunkStream::Resource {
        std::vector<std::unique_ptr<render::Model>> models;
        std::vector<Instance> instances;
    };
    WorldAssets assets_;
    size_t uploadBytes_=16*1024*1024;
    std::map<Cell,std::shared_ptr<Prepared>> resident_;
    std::unique_ptr<AsyncChunkStream> stream_;
    std::shared_ptr<Prepared> prepare(Cell,const AssetReader&);
};
}
