#pragma once
// Stage 11: batch several immutable cave meshes into ONE existing GeometryScene.
// Full-scene rebuild is deliberately explicit; no claim of incremental STRATA
// upload/retirement or atomic physics publication.
#include "engine/render/TerraWeaveCavernsGeometryScene.h"
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {

struct CaveBatchChunk {
    CaveMesh mesh;
    uint64_t generatorVersion=1;
    uint64_t revision=1; // visual identity
    uint64_t sourceRevision=0; // 0 means legacy unknown SDF epoch
    uint32_t resolution=16;
    int32_t spacingCm=100;
};
struct CaveBatchSpec {
    uint32_t maxChunks=64;
    size_t maxVertices=2000000;
    bool enableStrata=false;
};
struct CaveBatchPrepared {
    Json manifest;
    std::map<std::string,std::unique_ptr<render::Model>> models;
    size_t vertexCount=0;
    size_t triangleCount=0;
};
inline CaveBatchPrepared prepareCaveBatch(
    std::vector<CaveBatchChunk> chunks,CaveBatchSpec options={}) {
    if(!options.maxChunks||options.maxChunks>1024||
       !options.maxVertices||options.maxVertices>16000000 ||
       chunks.empty()||chunks.size()>options.maxChunks)
        throw std::invalid_argument("Caverns batch limits");
    std::sort(chunks.begin(),chunks.end(),[](const auto& a,const auto& b){
        return KeyLess{}(a.mesh.key,b.mesh.key);
    });
    for(size_t i=1;i<chunks.size();++i)
        if(chunks[i-1].mesh.key==chunks[i].mesh.key)
            throw std::invalid_argument("Caverns duplicate batch chunk");
    CaveBatchPrepared out;
    Json instances=Json::array();
    for(const auto& c:chunks) {
        if(!c.generatorVersion||!c.revision||
           c.resolution==0||c.resolution>64||
           c.spacingCm<=0||c.spacingCm>10000)
            throw std::invalid_argument("Caverns invalid batch entry");
        // Empty chunks produce no draw source; they are still useful for
        // authoritative collision/stream intent and are handled elsewhere.
        if(c.mesh.indices.empty()&&c.mesh.vertices.empty())continue;
        if(c.mesh.indices.empty()||c.mesh.vertices.empty())
            throw std::invalid_argument("Caverns incomplete batch mesh");
        if(c.mesh.vertices.size()>options.maxVertices-out.vertexCount)
            throw std::length_error("Caverns batch vertex budget");
        const auto key=caveGeometrySourceKey(c.mesh.key,c.generatorVersion,c.revision);
        const int64_t span=int64_t(c.resolution)*c.spacingCm;
        // Mesh space is cm-based world origin converted to metres; preserve
        // sign and order across negative keys.
        Json pos=Json::array();
        pos.push(Json::number(double(c.mesh.key.x)*double(span)/100.));
        pos.push(Json::number(double(c.mesh.key.y)*double(span)/100.));
        pos.push(Json::number(double(c.mesh.key.z)*double(span)/100.));
        Json instance=Json::object();
        instance.set("model",Json::string(key));
        instance.set("position",std::move(pos));
        instance.set("strata",Json::fromBool(options.enableStrata));
        instances.push(std::move(instance));
        out.vertexCount+=c.mesh.vertices.size();
        out.triangleCount+=c.mesh.indices.size()/3;
        out.models.emplace(key,makeCaveGeometryModel(c.mesh));
    }
    // All-empty residency should NOT instantiate GeometryScene: it requires
    // at least one valid source. The host can retire its old presentation.
    if(out.models.empty())return out;
    Json strata=Json::object();
    strata.set("enabled",Json::fromBool(options.enableStrata));
    strata.set("select",Json::string("all"));
    Json hlod=Json::object();
    hlod.set("enabled",Json::fromBool(false));
    out.manifest=Json::object();
    out.manifest.set("instances",std::move(instances));
    out.manifest.set("strata",std::move(strata));
    out.manifest.set("hlod",std::move(hlod));
    return out;
}

class CaveBatchGeometryResident {
public:
    bool resident() const {return scene_!=nullptr;}
    size_t sourceCount()const{return sourceCount_;}
    uint64_t batchRevision()const{return batchRevision_;}
    // This is called only from graphics owner and OUTSIDE render passes.
    // Rebuilding all sources may stall; use coarse batches and external
    // scheduling. A successful stage is not physical/navmesh commit proof.
    bool publish(gfx::Device& device,std::vector<CaveBatchChunk> chunks,
                 uint64_t batchRevision,CaveBatchSpec options={}) {
        if(!batchRevision)throw std::invalid_argument("Caverns batch revision");
        auto prepared=prepareCaveBatch(std::move(chunks),options);
        if(prepared.models.empty())return false;
        auto staged=std::make_unique<render::GeometryScene>();
        staged->setStrataStagingEnabled(options.enableStrata);
        staged->setStrataPortableRenderingEnabled(options.enableStrata);
        staged->loadPrepared(device,prepared.manifest,
                             std::move(prepared.models),options.maxVertices);
        // Only after the candidate is valid, retire the previous scene.
        // GeometryScene::shutdown has a full-idle requirement: this is
        // NOT cheap per-frame streaming; owner must serialize/lifetime gate.
        if(scene_)scene_->shutdown(device);
        scene_.swap(staged);
        sourceCount_=scene_->instanceCount();
        batchRevision_=batchRevision;
        return true;
    }
    void draw(render::Renderer3D& renderer){
        if(scene_)scene_->draw(renderer);
    }
    const render::GeometryScene* scene()const{return scene_.get();}
    void shutdown(gfx::Device& device){
        if(scene_)scene_->shutdown(device);
        scene_.reset();sourceCount_=0;batchRevision_=0;
    }
private:
    std::unique_ptr<render::GeometryScene> scene_;
    size_t sourceCount_=0;
    uint64_t batchRevision_=0;
};
} // namespace aiverse::terraweave::caverns
