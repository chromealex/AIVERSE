#pragma once
// Stage 10 opt-in ingestion into the ACTUAL GeometryScene/STRATA pipeline.
// Each adapter owns a distinct GeometryScene. NEVER also draw the same cave
// model with CutTerrainResident while this scene is being drawn.
#include "engine/world/TerraWeaveCavernsMesh.h"
#include "engine/render/GeometryScene.h"
#include <cmath>
#include <memory>
#include <map>
#include <string>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
inline std::string caveGeometrySourceKey(ChunkKey3 key,uint64_t generatorVersion,
                                         uint64_t revision){
    return "terraweave/caverns/"+std::to_string(key.x)+"/"+
        std::to_string(key.y)+"/"+std::to_string(key.z)+"/g"+
        std::to_string(generatorVersion)+"/r"+std::to_string(revision);
}
inline std::unique_ptr<render::Model> makeCaveGeometryModel(const CaveMesh& mesh) {
    if(mesh.vertices.empty()||mesh.indices.empty()||
       mesh.vertices.size()>2000000 ||mesh.indices.size()%3)
        throw std::invalid_argument("Caverns GeometryScene source mesh");
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/caverns/volume";
    model->batching.autoLod=false; // STRATA owns visual LOD
    model->batching.allowGeometryPool=true;
    render::Material material;
    material.name="Caverns Rock";
    material.baseColor={0.35f,0.32f,0.28f,1.f};
    material.roughness=0.92f;
    material.doubleSided=true;
    model->materials.push_back(std::move(material));
    model->vertices.reserve(mesh.vertices.size());
    for(const auto& p:mesh.vertices){
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
           !std::isfinite(p.nx)||!std::isfinite(p.ny)||!std::isfinite(p.nz))
            throw std::invalid_argument("Caverns nonfinite vertex");
        render::ModelVertex v;
        v.pos={p.x,p.y,p.z}; v.nrm={p.nx,p.ny,p.nz};
        v.uv={p.x*.05f,p.z*.05f};
        model->vertices.push_back(v);
    }
    for(uint32_t index:mesh.indices) {
        if(index>=model->vertices.size())
            throw std::invalid_argument("Caverns mesh index out of range");
    }
    model->indices=mesh.indices;
    render::Primitive prim;
    prim.material=0;prim.firstIndex=0;prim.indexCount=uint32_t(mesh.indices.size());
    prim.firstVertex=0;prim.vertexCount=uint32_t(mesh.vertices.size());
    render::MeshDef def;def.name="Caverns Volume";def.primitives.push_back(std::move(prim));
    model->meshes.push_back(std::move(def));
    render::Node root;root.name="Caverns";root.mesh=0;
    model->nodes.push_back(std::move(root));
    model->finalize();
    return model;
}
inline Json caveGeometrySceneManifest(ChunkKey3 key,uint64_t generatorVersion,
                                      uint64_t revision,uint32_t resolution,
                                      int32_t spacingCm,bool enableStrata){
    if(!generatorVersion||!revision||resolution==0||spacingCm<=0)
        throw std::invalid_argument("Caverns scene manifest");
    const int64_t span=int64_t(resolution)*spacingCm;
    const double scale=0.01;
    Json position=Json::array();
    position.push(Json::number(double(key.x)*double(span)*scale));
    position.push(Json::number(double(key.y)*double(span)*scale));
    position.push(Json::number(double(key.z)*double(span)*scale));
    Json instance=Json::object();
    instance.set("model",Json::string(caveGeometrySourceKey(key,generatorVersion,revision)));
    instance.set("position",std::move(position));
    instance.set("strata",Json::fromBool(enableStrata));
    Json instances=Json::array();instances.push(std::move(instance));
    Json strata=Json::object();
    strata.set("enabled",Json::fromBool(enableStrata));
    strata.set("select",Json::string("all"));
    Json hlod=Json::object();hlod.set("enabled",Json::fromBool(false));
    Json scene=Json::object();
    scene.set("instances",std::move(instances));
    scene.set("strata",std::move(strata));
    scene.set("hlod",std::move(hlod));
    return scene;
}
// The actual GeometryScene::loadPrepared uploads and owns this model;
// reloads retire resources via GeometryScene's existing fence-aware path.
// One adapter per live cave chunk is intentionally simple, but expensive:
// production should batch multiple chunks in ONE scene for scalable streaming.
class CaveGeometrySceneResident {
public:
    bool resident()const{return ready_;}
    uint64_t revision()const{return revision_;}
    bool publish(gfx::Device& device,CaveMesh mesh,uint64_t generatorVersion,
                 uint64_t revision,uint32_t resolution,int32_t spacingCm,
                 bool strataEnabled){
        if(!revision||!generatorVersion ||mesh.indices.empty())return false;
        auto key=mesh.key;
        auto name=caveGeometrySourceKey(key,generatorVersion,revision);
        auto json=caveGeometrySceneManifest(key,generatorVersion,revision,
                                             resolution,spacingCm,strataEnabled);
        std::map<std::string,std::unique_ptr<render::Model>> models;
        models.emplace(name,makeCaveGeometryModel(mesh));
        // A temporary scene protects the previous live scene if staging fails.
        auto staged=std::make_unique<render::GeometryScene>();
        staged->setStrataStagingEnabled(strataEnabled);
        staged->setStrataPortableRenderingEnabled(strataEnabled);
        staged->loadPrepared(device,json,std::move(models),2000000);
        // Actual GeometryScene controls whether STRATA accepts and draws the
        // mesh; do NOT assume requested STRATA was accepted.
        if(scene_)scene_->shutdown(device); // Requires full GPU idle proof; owner barrier only
        scene_=std::move(staged);key_=key;revision_=revision;
        ready_=true;return true;
    }
    void draw(render::Renderer3D& renderer) {
        if(scene_&&ready_)scene_->draw(renderer);
    }
    const render::GeometryScene* scene()const{return scene_.get();}
    void shutdown(gfx::Device& device){
        if(scene_)scene_->shutdown(device);
        scene_.reset();ready_=false;revision_=0;
    }
private:
    std::unique_ptr<render::GeometryScene> scene_;
    ChunkKey3 key_{};
    uint64_t revision_=0;
    bool ready_=false;
};
} // namespace aiverse::terraweave::caverns
