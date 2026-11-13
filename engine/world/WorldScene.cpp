#include "WorldScene.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace aiverse::world {
namespace {
render::float3 visual(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
size_t modelBytes(const render::Model& m) {
    // CPU + GPU + node/material overhead, including mip chains and upload staging allowance.
    size_t bytes=4096+m.vertices.capacity()*sizeof(render::ModelVertex)*3+m.indices.capacity()*sizeof(uint32_t)*3;
    bytes+=m.nodes.size()*512+m.materials.size()*1024;
    for(const auto& image:m.images){if(image.width<0||image.height<0||image.width>8192||image.height>8192)throw JsonError("world.mesh: image exceeds 8192");bytes+=size_t(image.width)*size_t(image.height)*16+image.rgba.capacity();}
    return bytes;
}
std::unique_ptr<render::Model> terrainModel(const ChunkCollision& collision,const Json& definition,const AssetReader& reader,Vec3 origin) {
    if(!collision.terrain.resolution)return {};
    // The tail contains terrain triangles in the exact collision diagonal/order.
    const size_t count=size_t(collision.terrain.resolution-1)*size_t(collision.terrain.resolution-1)*2;
    auto model=std::make_unique<render::Model>();model->path="world:terrain";
    render::Material material;material.baseColor={.22f,.38f,.16f,1};material.doubleSided=true;
    const auto& authored=definition["material"];
    if(authored.has("color")){auto rgb=visual(worldVector(authored["color"],1_fx));if(rgb.x<0||rgb.y<0||rgb.z<0)throw JsonError("world.terrain.material: negative color");material.baseColor={rgb.x,rgb.y,rgb.z,1};}
    auto texture=[&](const char* key,int& index){if(!authored.has(key))return;auto file=authored.getStr(key);std::string bytes;
        if(!safeAssetPath(file)||!reader(file,bytes))throw JsonError("world.terrain: missing or corrupt texture");
        render::Image image;image.name=file;if(!render::decodeImage(bytes,image))throw JsonError("world.terrain: unsupported texture");
        image.repeat=true;index=int(model->images.size());model->images.push_back(std::move(image));};
    texture("baseTexture",material.baseTex);texture("normalTexture",material.normalTex);
    model->materials.push_back(material);
    const auto uv=definition.getFx("uvScale",Fixed::fromRatio(1,20));
    if(uv<=0_fx||uv>16_fx)throw JsonError("world.terrain.uvScale: expected 0..16");
    const float uvScale=float(uv.toDouble());
    const float uOffset=float(Fixed::fromRaw((origin.x*uv).raw%Fixed::kOne).toDouble());
    const float vOffset=float(Fixed::fromRaw((origin.z*uv).raw%Fixed::kOne).toDouble());
    render::Node node;node.mesh=0;model->nodes.push_back(node);model->meshes.emplace_back();
    for(size_t i=collision.triangles.size()-count;i<collision.triangles.size();++i){const auto& t=collision.triangles[i];
        auto a=visual(t.a),b=visual(t.b),c=visual(t.c);auto normal=render::normalize(render::cross(b-a,c-a));
        for(auto p:{a,b,c}){render::ModelVertex v;v.pos=p;v.nrm=normal;v.uv={p.x*uvScale+uOffset,p.z*uvScale+vOffset};model->indices.push_back(uint32_t(model->vertices.size()));model->vertices.push_back(v);}}
    render::Primitive p;p.indexCount=uint32_t(model->indices.size());p.vertexCount=uint32_t(model->vertices.size());model->meshes[0].primitives.push_back(p);model->finalize();return model;
}
}
// Models shared between chunks ("shareModels": true): a city built from a kit repeats the same files in every chunk,
// so each file is decoded and uploaded once and lives while any resident or pending chunk uses it. Shared models are
// outside the per-chunk reservation: their total is bounded by the number of distinct files of the content.
struct WorldScene::ModelCache { std::mutex mutex;std::map<std::string,std::weak_ptr<render::Model>> models; };
WorldScene::WorldScene(const Json& manifest,AssetReader reader):assets_(WorldAssets::parse(manifest)),cache_(std::make_shared<ModelCache>()) {
    shareModels_=manifest.getBool("shareModels");
    overhang_=float(worldInteger(manifest,"meshOverhang",0,0,4096));
    uploadBytes_=size_t(worldInteger(manifest,"maxUploadBytes",16*1024*1024,1024,1024*1024*1024));
    auto verified=assets_.verifiedReader(reader);
    stream_=std::make_unique<AsyncChunkStream>(assets_.limits,assets_.stagingBytes,assets_.maxJobs,[this,verified](Cell cell){return prepare(cell,verified);});
    for(const auto& [cell,asset]:assets_.chunks){if(asset.bytes>uploadBytes_)throw JsonError("world.chunk: reservation exceeds maxUploadBytes");if(!stream_->add(cell,asset.bytes))throw JsonError("world.chunk: staging budget too small");}
}
WorldScene::~WorldScene()=default;
std::shared_ptr<WorldScene::Prepared> WorldScene::prepare(Cell cell,const AssetReader& reader) {
    const auto& asset=assets_.chunks.at(cell);std::string text;
    if(!reader(asset.file,text))throw JsonError("world.scene: read/hash failure: "+asset.file);
    auto json=Json::parse(text);auto collision=readChunkCollision(json,assets_.cellSize);
    auto out=std::make_shared<Prepared>();size_t bytes=4096;
    auto append=[&](std::shared_ptr<render::Model> model,const std::string& sharedKey=std::string(),bool charge=true,bool fresh=true){
        // a model reused from the cache was checked when it was decoded (and its CPU arrays are gone after the upload)
        if(!fresh){bytes+=4096;if(bytes>asset.bytes)throw JsonError("world.scene: reservedBytes too small");out->models.push_back(std::move(model));out->sharedKeys.push_back(sharedKey);return out->models.size()-1;}
        if(model->vertices.size()>1000000||model->indices.size()>3000000||!model->skins.empty()||!model->clips.empty())throw JsonError("world.mesh: expected bounded rigid model");
        for(const auto& mesh:model->meshes)for(const auto& p:mesh.primitives)if(p.morphTargets)throw JsonError("world.mesh: morphs require an entity");
        if(model->vertices.empty()||model->indices.empty())throw JsonError("world.mesh: empty model");
        bytes+=charge?modelBytes(*model):size_t(4096);if(bytes>asset.bytes)throw JsonError("world.scene: reservedBytes too small");
        out->models.push_back(std::move(model));out->sharedKeys.push_back(sharedKey);return out->models.size()-1;
    };
    if(auto terrain=terrainModel(collision,json["terrain"],reader,assets_.origin(cell))){auto index=append(std::move(terrain));out->instances.push_back({index,{}});}
    if(json.has("meshes")) {
        if(!json["meshes"].isArray()||json["meshes"].size()>256)throw JsonError("world.meshes: expected at most 256 instances");
        std::map<std::string,size_t> indices;
        for(const auto& instance:json["meshes"].items()){
            auto file=instance.getStr("file");if(!safeAssetPath(file))throw JsonError("world.mesh: invalid path");
            if(!indices.contains(file)&&shareModels_){
                std::shared_ptr<render::Model> shared;
                {std::lock_guard lock(cache_->mutex);if(auto it=cache_->models.find(file);it!=cache_->models.end())shared=it->second.lock();}
                if(shared)indices[file]=append(std::move(shared),file,false,false);
            }
            if(!indices.contains(file)){
                bool missing=false;
                auto checked=[&](const std::string& path,std::string& bytes){bool ok=reader(path,bytes);missing|=!ok;return ok;};
                auto model=render::loadModel(file,checked);
                if(missing)throw JsonError("world.mesh: missing or corrupt dependency: "+file);
                std::shared_ptr<render::Model> owned=std::move(model);
                if(shareModels_){std::lock_guard lock(cache_->mutex);cache_->models[file]=owned;}
                indices[file]=append(std::move(owned),shareModels_?file:std::string(),!shareModels_);
            }
            auto position=instance.has("position")?visual(worldVector(instance["position"],Fixed::fromInt(assets_.cellSize))):render::float3{};
            auto scale=instance.has("scale")?visual(worldVector(instance["scale"],100_fx)):render::float3{1,1,1};
            if(scale.x<=0||scale.y<=0||scale.z<=0)throw JsonError("world.mesh: scale must be positive");
            auto rotation=instance.has("rotation")?visual(worldVector(instance["rotation"],360_fx)):render::float3{};
            constexpr float deg=.0174532925199433f;
            auto transform=render::mat4::translate(position)*render::mat4::rotateY(rotation.y*deg)*render::mat4::rotateX(rotation.x*deg)*render::mat4::rotateZ(rotation.z*deg)*render::mat4::scale(scale);
            const auto& model=*out->models[indices.at(file)];
            for(int corner=0;corner<8;++corner){auto a=model.boundsMin,b=model.boundsMax;
                auto p=transform.transformPoint({corner&1?b.x:a.x,corner&2?b.y:a.y,corner&4?b.z:a.z});
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.x<-overhang_||p.z<-overhang_||p.x>assets_.cellSize+overhang_||p.z>assets_.cellSize+overhang_||std::abs(p.y)>10000)
                    throw JsonError("world.mesh: transformed bounds must fit owning X/Z cell");
            }
            out->instances.push_back({indices.at(file),transform});
        }
    }
    return out;
}
// GPU resources of a chunk that leaves: its own models always, a shared model only when no other chunk (resident or
// being prepared) holds it; the cache entry goes first, under the lock, so a worker cannot pick up a released model.
void WorldScene::releaseChunk(gfx::Device& device,Prepared& chunk) {
    for(size_t i=0;i<chunk.models.size();++i){
        auto& model=chunk.models[i];
        if(i<chunk.sharedKeys.size()&&!chunk.sharedKeys[i].empty()){
            std::lock_guard lock(cache_->mutex);
            if(model.use_count()>1)continue;
            cache_->models.erase(chunk.sharedKeys[i]);
        }
        render::releaseModel(device,*model);
    }
}
void WorldScene::update(gfx::Device& device,std::span<const Vec3> observers,int radius) {
    std::vector<StreamObserver> interests;for(auto p:observers)interests.push_back({assets_.cell(p),radius});
    stream_->update(interests,{},4,2,[&](Cell cell,const AsyncChunkStream::Payload& payload){
        auto prepared=std::static_pointer_cast<Prepared>(payload);
        try {
            for(auto& model:prepared->models){
                if(model->vb.valid()&&model->ib.valid())continue;   // shared with a resident chunk: already on the GPU
                if(!render::uploadModel(device,*model)||!model->vb.valid()||!model->ib.valid())throw JsonError("world.mesh: GPU buffer upload failed");
                for(auto& image:model->images)if(!image.tex.valid())throw JsonError("world.mesh: GPU texture upload failed");
            }
            resident_[cell]=prepared;return true;
        }catch(...){releaseChunk(device,*prepared);throw;}
    },[&](Cell cell){auto it=resident_.find(cell);if(it==resident_.end())return;releaseChunk(device,*it->second);resident_.erase(it);},uploadBytes_);
}
void WorldScene::draw(render::Renderer3D& renderer,Cell origin,int size) {
    for(const auto& [cell,chunk]:resident_){const auto offset=render::mat4::translate(visual(relativeTo(assets_.origin(cell),origin,size)));
        for(const auto& instance:chunk->instances){render::ModelDraw draw;draw.model=chunk->models[instance.model].get();draw.world=offset*instance.transform;renderer.drawModel(draw);}}
}
void WorldScene::shutdown(gfx::Device& device) {
    stream_->shutdown([&](Cell cell){auto it=resident_.find(cell);if(it!=resident_.end()){releaseChunk(device,*it->second);resident_.erase(it);}});
}
}
