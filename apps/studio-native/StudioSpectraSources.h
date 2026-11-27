// Native Studio adapter: authored flat-map sources -> existing ECS presenter.
// Device and frame ownership remain with StudioScene.
#pragma once
#include "libraries/include/aiverselib/mechanics/SpectraSourceMirror.h"
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraSceneVolumetricParticles.h"
#include "engine/fx/SpectraSdfFileIO.h"
#include "engine/fx/SpectraScenePoolEmission.h"
#include "libraries/include/aiverselib/mechanics/SpectraGpuAttachmentEcsAdapter.h"
#include "engine/fx/FxModelMesh.h"
#include "engine/render/TextureCache.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include "apps/studio-native/StudioSpectraFields.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>

namespace aiverse::studio {
class StudioSpectraSources {
public:
    void shutdown(){
        if(presenter_)presenter_->reset();
        finish();
        gpuPresenter_.reset();gpuMirror_.clear();gpuPool_.shutdown();gpuDefinitions_.clear();
        presenter_.reset();mirror_.clear();
        particles_.shutdown();textures_.shutdown();
        volumeWorld_.clear();volumetricWorld_.clear();volumeDefinitions_.clear();volumeOnly_.clear();hybridDefinitions_.clear();
        volumetricDefinitions_.clear();volumetricDepth_.clear();
        definitions_.clear();schemas_.clear();project_.clear();mapPath_.clear();error_.clear();
        dataRegistry_.reset();externalRegistry_=false;
        collisionRegistry_.reset();
        revision_=UINT64_MAX;ready_=false;
    }
    // Render owner only, between frames. Shared ownership prevents dangling
    // callbacks when a host publishes a fresh immutable registry.
    bool setDataInterfaceRegistry(std::shared_ptr<const spectra::DataInterfaceRegistry> registry){
        const bool external=bool(registry);
        if(!installRegistry(std::move(registry)))return false;
        externalRegistry_=external;
        if(!external)revision_=UINT64_MAX;
        return true;
    }
private:
    bool installRegistry(std::shared_ptr<const spectra::DataInterfaceRegistry> registry){
        if(gpuFrameOpen_)return false;
        if(gpuPool_.ready()&&!gpuPool_.setDataInterfaceRegistry(registry.get()))return false;
        if(presenter_)presenter_->setDataInterfaceRegistry(registry.get());
        dataRegistry_=std::move(registry);
        return true;
    }
public:
    bool animated() const{
        return ready_&&(mirror_.size()!=0||particles_.stats().particles>0||
            gpuMirror_.size()!=0||gpuPool_.stats().gpuParticlesEstimated>0||gpuPool_.stats().cpuParticles>0||
            volumeWorld_.active()||volumetricWorld_.active());
    }
    const std::string& error()const{return error_;}
    bool hasVolumes()const{return volumeWorld_.renderable()||volumeWorld_.hasDepthParticles();}
    bool hasVolumetricDepth()const{return volumetricWorld_.hasDepth();}
    gfx::TextureHandle composeVolumes(gfx::Device& d,const render::PostFrame& f,gfx::TextureHandle color,gfx::TextureHandle depth){return volumeWorld_.compose(d,f,color,depth);}
    gfx::TextureHandle composeVolumetricDepth(gfx::Device& d,const render::PostFrame& f,gfx::TextureHandle color,gfx::TextureHandle depth){return volumetricWorld_.composeDepth(d,f,color,depth);}
    void synchronize(gfx::Device& device,const editor::MapDoc& doc,const std::string& project,
                     const std::set<std::string>& hidden,
                     float levelHeight=1.5f,bool showTerrain=true,
                     const world::NereidWaterScene* water=nullptr){
        if(project!=project_||doc.path!=mapPath_){
            device.waitForResourceIdle();shutdown();
            project_=project;mapPath_=doc.path;
        }
        if(project.empty()||doc.data.worldSpace||doc.data.planet()){
            volumeWorld_.clear();
            volumetricWorld_.clear();
            if(ready_){mirror_.clear();gpuMirror_.clear();}
            if(!externalRegistry_)installRegistry({});
            collisionRegistry_.reset();particles_.setSceneCollisionRegistry({});
            gpuPool_.discardCollisionChildren();
            revision_=UINT64_MAX;
            return;
        }
        // Rebuild authored snapshots only with document/terrain changes.
        const bool fieldChanged=revision_!=doc.revision||fieldLevelHeight_!=levelHeight||
                                fieldShowTerrain_!=showTerrain;
        if(fieldChanged && !externalRegistry_) {
            auto next=buildStudioSpectraFields(doc.data,levelHeight,showTerrain,water);
            if(!installRegistry(std::move(next)))return;
        }
        if(fieldChanged){
            collisionRegistry_=buildStudioSpectraCollisions(doc.data,levelHeight,showTerrain);
            particles_.setSceneCollisionRegistry(collisionRegistry_);
        }
        if(!fieldChanged&&hidden_==hidden)return;
        revision_=doc.revision;hidden_=hidden;
        fieldLevelHeight_=levelHeight;fieldShowTerrain_=showTerrain;
        std::vector<lib::mechanics::SpectraSourceSnapshot> rows,gpuRows;
        std::vector<spectra::volume::SceneVolumeSource> volumes;
        std::vector<spectra::SceneVolumetricParticleSource> volumetricParticles;
        std::set<std::string> seen;
        error_.clear();
        for(size_t i=0;i<doc.data.entities.size()&&rows.size()+gpuRows.size()+volumes.size()+volumetricParticles.size()<65536;++i){
            const auto& entity=doc.data.entities[i];
            const auto& value=entity.extra["components"]["SpectraSource"];
            if(!value.isObject()||!entity.at.isNull())continue;
            try{
                auto source=lib::mechanics::readSpectraSourceDocument(value);
                if(!source.enabled||source.effect.empty())continue;
                auto identity=studioDocumentId(doc,editor::DocumentEntityRef::mapEntity(i));
                if(hidden.count(identity)||!seen.insert(identity).second)continue;
                // Ordinal identities cannot survive structural document changes.
                if(entity.id.empty())identity+=":"+std::to_string(doc.revision);
                if(!ready_&&!initialize(device))return;
                if(!define(source.effect))continue;
                std::array<float,8> values{};
                if(!lib::mechanics::resolveSpectraSourceParameters(source,schemas_.at(source.effect),values)){
                    error_="Invalid instance parameter override: "+source.effect;continue;
                }
                const auto transform=editor::authoredLocalTransform(entity);
                const auto p=transform.position;
                if(const auto authored=volumetricDefinitions_.find(source.effect);authored!=volumetricDefinitions_.end()){
                    spectra::SceneVolumetricParticleSource row;
                    row.identity=identity;row.effect=source.effect;row.revision=source.revision;
                    row.position={float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};
                    row.scale=float(source.scale.toDouble());row.graph=&authored->second;
                    row.depthAware=volumetricDepth_.at(source.effect);
                    for(const auto& [name,value]:source.parameters)row.parameters.emplace(name,float(value.toDouble()));
                    volumetricParticles.push_back(std::move(row));continue;
                }
                if(auto it=volumeDefinitions_.find(source.effect);it!=volumeDefinitions_.end()){
                    spectra::volume::SceneVolumeSource row;
                    row.identity=identity;row.effect=source.effect;row.revision=source.revision;
                    row.anchor={float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};
                    row.asset=&it->second;
                    row.scale=float(source.scale.toDouble());
                    if(auto hybrid=hybridDefinitions_.find(source.effect);hybrid!=hybridDefinitions_.end()){
                        row.hybrid=&hybrid->second;
                        for(const auto& [name,value]:source.parameters)row.parameters.emplace(name,float(value.toDouble()));
                    }
                    volumes.push_back(std::move(row));
                    if(hybridDefinitions_.count(source.effect))continue;
                    if(volumeOnly_.count(source.effect))continue;
                }
                lib::mechanics::SpectraSourceSnapshot row{identity,source,{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())},{0,1,0}};
                if(source.scale.raw==(1_fx).raw&&gpuDefinitions_.count(source.effect))gpuRows.push_back(std::move(row));
                else rows.push_back(std::move(row));
            }catch(const std::exception& e){error_=e.what();}
        }
        volumeWorld_.synchronize(device,volumes,fx::projectSdfReader(project_));
        volumetricWorld_.synchronize(device,volumetricParticles,fx::projectSdfReader(project_));
        if(!volumeWorld_.error().empty())error_=volumeWorld_.error();
        if(!volumetricWorld_.error().empty())error_=volumetricWorld_.error();
        mirror_.sync(rows);gpuMirror_.sync(gpuRows);
        // Retain loaded definitions while their detached particles fade.
        // Definition count is bounded for this document session.
    }
    void prepare(const fx::SpectraFxView& view,float dt){
        if(!ready_||!view.valid())return;
        volumeWorld_.setView(view);volumeWorld_.applyEnvironment(dataRegistry_.get());volumeWorld_.step(dt);
        volumetricWorld_.prepare(view,dt);
        if(!volumeWorld_.error().empty())error_=volumeWorld_.error();
        if(!volumetricWorld_.error().empty())error_=volumetricWorld_.error();
        particles_.setView(view);
        presenter_->sync(mirror_.world(),lib::mechanics::SpectraSourceMirror::resolve);
        if(!presenter_->dataInterfaceError().empty())error_=presenter_->dataInterfaceError();
        if(gpuPool_.ready()&&gpuMirror_.size())gpuPool_.drainCollisionChildren(particles_);
        else gpuPool_.discardCollisionChildren();
        particles_.update(dt);
        if(gpuPresenter_&&gpuPool_.ready()){
            if(!gpuPool_.beginPresentationFrame(view)){error_=gpuPool_.reason();return;}
            gpuFrameOpen_=true;
            if(!gpuPresenter_->sync(gpuMirror_.world(),lib::mechanics::SpectraSourceMirror::resolve)){
                error_=gpuPresenter_->reason();finish();return;
            }
            if(!gpuPool_.step(dt)){error_=gpuPool_.reason();finish();}
        }
    }
    void draw(render::Renderer3D* renderer=nullptr) {
        if(ready_) {
            if(renderer)particles_.renderScene(*renderer,{});else particles_.render({});
            volumeWorld_.drawParticles({},renderer);volumetricWorld_.drawInline({},renderer);
        }
        if(gpuFrameOpen_)gpuPool_.draw({},renderer);
    }
    void finish(){if(gpuFrameOpen_){gpuPool_.endPresentationFrame();gpuFrameOpen_=false;}}
private:
    static constexpr uint32_t kStudioMaxGroupSlots=65536;
    bool initialize(gfx::Device& device){
        textures_.init(device);
        particles_.setFileReader([this](const std::string& path,std::string& out){return read(path,out);});
        particles_.setTextureLoader([this](const std::string& path,fx::FxTexture& out){
            if(safeFile(path).empty())return false;
            auto tex=textures_.resolve(path,false,project_+"/");
            if(!tex.valid())return false;
            out={tex.tex,tex.u0,tex.v0,tex.u1,tex.v1};return true;
        });
        particles_.setMeshLoader([this](const std::string& path,fx::FxMesh& out){
            return fx::meshFromModelFile(path,[this](const std::string& p,std::string& bytes){return read(p,bytes);},out);
        });
        if(!particles_.init(&device)){error_=particles_.lastError();textures_.shutdown();return false;}
        particles_.setSceneCollisionRegistry(collisionRegistry_);
        presenter_=std::make_unique<lib::mechanics::SpectraPresentationSystem>(particles_);
        spectra::PoolWorldBudgets budgets;
        budgets.maxGpuGroups=256;budgets.maxGroupCapacity=kStudioMaxGroupSlots;budgets.maxSources=65536;
        budgets.maxBurstsPerFrame=16384;
        budgets.maxGpuAllocationBytes=512ull*1024*1024;
        presenter_->setDataInterfaceRegistry(dataRegistry_.get());
        if(gpuPool_.init(&device,budgets)){
            gpuPool_.setDataInterfaceRegistry(dataRegistry_.get());
            gpuPresenter_=std::make_unique<spectra::GpuEcsAttachmentPresenter>(gpuPool_);
        }
        ready_=true;return true;
    }
    std::filesystem::path safeFile(const std::string& path)const{
        namespace fs=std::filesystem;
        if(path.empty()||path.size()>512||path.find('\\')!=std::string::npos||path.find(':')!=std::string::npos||
           (path.rfind("content/",0)!=0&&path.rfind("fx/",0)!=0))return {};
        fs::path file=project_;
        std::error_code ec;
        for(const auto& part:fs::path(path)){
            if(part=="."||part==".."||part.empty())return {};
            file/=part;
            if(fs::is_symlink(fs::symlink_status(file,ec))||ec)return {};
        }
        return fs::is_regular_file(file,ec)&&!ec?file:fs::path{};
    }
    bool read(const std::string& path,std::string& out)const{
        const auto file=safeFile(path);
        if(file.empty()||std::filesystem::file_size(file)>128ull*1024*1024)return false;
        std::ifstream input(file,std::ios::binary);
        if(!input)return false;
        out.assign(std::istreambuf_iterator<char>(input),{});
        return !input.bad()&&out.size()<=128ull*1024*1024;
    }
    bool define(const std::string& name){
        if(definitions_.count(name))return true;
        if(definitions_.size()>=4096){error_="SpectraFX scene definition limit reached";return false;}
        std::string text;
        if(!read("fx/graphs/"+name+".vfx.json",text)||text.size()>512*1024){
            error_="Missing or oversized SpectraFX graph: "+name;return false;
        }
        auto graph=Json::parse(text);
        spectra::volume::PresentationGraph compiled;std::vector<std::string> errors;
        if(graph.getInt("version")!=2||!spectra::volume::compilePresentationGraph(graph,Json::object(),compiled,errors,fx::projectSdfReader(project_))){
            error_=errors.empty()?"Invalid SpectraFX graph: "+name:errors.front();return false;
        }
        if(compiled.particleMaterial.enabled){
            spectra::InstanceParameterTable named;
            if(!spectra::loadInstanceParameterTable(compiled.particleGraph["instanceParameters"],named,errors)){
                error_=errors.empty()?"Invalid SpectraFX parameter schema":errors.front();return false;
            }
            if(compiled.volume){
                hybridDefinitions_[name]={std::move(compiled.particleGraph),compiled.coupling,compiled.particleMaterial.depthAware};
                volumeDefinitions_[name]=std::move(compiled.volumeAsset);
                schemas_[name]=std::move(named);definitions_.insert(name);return true;
            }
            volumetricDefinitions_[name]=std::move(compiled.particleGraph);
            volumetricDepth_[name]=compiled.particleMaterial.depthAware;
            schemas_[name]=std::move(named);definitions_.insert(name);return true;
        }
        if(compiled.volume){
            if(!compiled.particles){volumeDefinitions_[name]=std::move(compiled.volumeAsset);volumeOnly_.insert(name);schemas_[name]={};definitions_.insert(name);return true;}
            graph=compiled.particleGraph;
        }
        auto& def=compiled.particleEffect;
        spectra::InstanceParameterTable named;
        if(!spectra::loadInstanceParameterTable(graph["instanceParameters"],named,errors)){
            error_=errors.empty()?"Invalid SpectraFX parameter schema":errors.front();return false;
        }
        if(compiled.coupling.active()){
            schemas_[name]=std::move(named);definitions_.insert(name);
            hybridDefinitions_[name]={graph,compiled.coupling};
            volumeDefinitions_[name]=std::move(compiled.volumeAsset);return true;
        }
        if(particles_.define(name,def)<0){error_="Cannot define SpectraFX graph: "+name;return false;}
        if(!presenter_->bindGraphDataInterfaces(name,graph,errors)){
            error_=errors.empty()?"Invalid SpectraFX Data Interfaces":errors.front();return false;
        }
        schemas_[name]=std::move(named);
        definitions_.insert(name);
        if(compiled.volume)volumeDefinitions_[name]=std::move(compiled.volumeAsset);
        // CPU definition always remains available for incompatible devices,
        // unscaled-only GPU policy failures and nonunit authored instances.
        if(gpuPresenter_){
            try{
                spectra::PoolContinuousEmission emission;
                spectra::GraphRuntimeOptions options;
                options.sdfReader=fx::projectSdfReader(project_);
                options.preference=spectra::ComputePreference::RequireGpu;options.gpuProgram=true;
                options.routePoolCollisionChildren=true;
                options.proceduralBillboards=true;options.gpuIndirect=true;
                // Opt-in until native GPU allocator parity/overflow tests pass.
                options.gpuOwnedAllocation=graph.getBool("sceneGpuAllocation",false);
                // Small default groups; opt in to larger allocations per graph.
                const auto& poolCapacity=graph["scenePoolCapacity"];
                options.capacity=16384;
                if(!poolCapacity.isNull()){
                    const auto slots=poolCapacity.integer(-1);
                    if(!poolCapacity.isNumber()||poolCapacity.numberLiteral().find_first_of(".eE-")!=std::string::npos||
                       slots<1||slots>kStudioMaxGroupSlots)
                        throw std::runtime_error("Invalid scenePoolCapacity");
                    options.capacity=uint32_t(slots);
                }
                if(spectra::readScenePoolEmission(graph,emission)&&
                   spectra::inspectPoolGraphGpuEligibility(def,options).eligible&&
                   gpuPool_.defineGraph(name,graph,Json::object(),options)&&
                   gpuPool_.isGpuEffect(name)&&gpuPresenter_->bindEffect(name,emission))
                    gpuDefinitions_.insert(name);
            }catch(const std::exception& e){error_=e.what();}
        }
        return true;
    }
    std::shared_ptr<const spectra::DataInterfaceRegistry> dataRegistry_;
    std::shared_ptr<const fx::SceneCollisionRegistry> collisionRegistry_;
    render::TextureCache textures_;
    fx::ParticleSystem particles_;
    lib::mechanics::SpectraSourceMirror mirror_;
    std::unique_ptr<lib::mechanics::SpectraPresentationSystem> presenter_;
    spectra::GpuPoolWorld gpuPool_;
    lib::mechanics::SpectraSourceMirror gpuMirror_;
    std::unique_ptr<spectra::GpuEcsAttachmentPresenter> gpuPresenter_;
    bool gpuFrameOpen_=false,externalRegistry_=false;
    float fieldLevelHeight_=0;
    bool fieldShowTerrain_=false;
    spectra::volume::SceneVolumeWorld volumeWorld_;
    spectra::SceneVolumetricParticleWorld volumetricWorld_;
    std::map<std::string,Json> volumetricDefinitions_;
    std::map<std::string,bool> volumetricDepth_;
    std::map<std::string,spectra::volume::Asset> volumeDefinitions_;
    std::set<std::string> volumeOnly_;
    std::map<std::string,spectra::volume::SceneHybridDefinition> hybridDefinitions_;
    std::set<std::string> definitions_,gpuDefinitions_,hidden_;
    std::map<std::string,spectra::InstanceParameterTable> schemas_;
    std::string project_,mapPath_,error_;
    uint64_t revision_=UINT64_MAX;
    bool ready_=false;
};
}
