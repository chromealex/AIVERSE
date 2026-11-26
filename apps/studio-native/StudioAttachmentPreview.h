#pragma once
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/view/ImportedAttachmentAuthoring.h"
#include "engine/view/AttachmentLayout.h"
#include "engine/view/EcsAttachmentScene.h"
#include "engine/view/SceneAttachmentHost.h"
#include "engine/view/AnimatedSceneAttachmentSources.h"
#include "engine/render/Animation.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>

namespace aiverse::studio {

class StudioAttachmentPreview {
    using Imported=view::ImportedAttachmentAuthoring;
    using Registry=view::AppearanceAssetRegistry;
    struct Session {
        bool originalModel=false;
        std::unique_ptr<render::ModelAssetProvider> provider;
        std::unique_ptr<Registry> registry;
        Registry::Snapshot source;
        // Cosmetic, source-neutral ECS world, never gameplay Simulation.
        World presentation;
        Entity parentEntity{};
        std::unique_ptr<view::SceneAttachmentHost> attachmentHost;
        std::map<std::string,Registry::Snapshot> animatedChildren;
        std::vector<Registry::Snapshot> children;
    };


    gfx::Device& device_;
    render::Renderer3D& renderer_;
    render::RenderDeviceLifetime::Lease lease_;
    std::unique_ptr<Session> session_;
    std::vector<std::unique_ptr<Session>> retired_;
    view::AnimatedSceneAttachmentSources animatedSources_;
    ::aiverse::View presentationView_,animatedPreviewView_;
    render::Pose previewPose_;
    std::vector<render::mat4> previewMatrices_;
    float time_=0,yaw_=.6f,pitch_=.4f;
    bool playing_=true;
    std::chrono::steady_clock::time_point last_{};

    static std::string resolve(const std::filesystem::path& root,const std::string& name){
        const std::filesystem::path path(name);
        if(path.empty()||path.is_absolute())throw std::runtime_error("Choose a project-relative asset");
        const auto file=std::filesystem::weakly_canonical(root/path);
        auto local=file.lexically_relative(root);
        if(local.empty()||local=="."||*local.begin()=="..")throw std::runtime_error("Asset is outside the project");
        return file.string();
    }
    void retire(){
        if(retired_.empty()||!device_.waitForResourceIdle())return;
        for(auto& old:retired_){
            auto forget=[&](const Registry::Snapshot& snapshot){
                if(snapshot&&snapshot->model)renderer_.invalidateModelCachesFor(snapshot->model);
            };
            forget(old->source);
            for(const auto& child:old->children)forget(child);
            for(const auto& [id,child]:old->animatedChildren)forget(child);
            old->attachmentHost.reset();old->children.clear();old->animatedChildren.clear();old->source.reset();
            old->registry.reset();old->provider->shutdown(device_);
        }
        retired_.clear();
    }
public:
    StudioAttachmentPreview(gfx::Device& device,render::Renderer3D& renderer,render::RenderDeviceLifetime::Lease lease)
        :device_(device),renderer_(renderer),lease_(std::move(lease)){}
    ~StudioAttachmentPreview(){
        animatedSources_.clear();
        if(session_)retired_.push_back(std::move(session_));
        try{retire();}catch(...){}
        for(auto& session:retired_)(void)session.release();
    }
    Json publish(const std::string& project,const Json& authored,bool originalModel){
        view::AttachmentLayout document;
        view::AttachmentLayoutCodec::read(authored,document);
        const auto root=std::filesystem::canonical(project);
        const auto withinProject=[&](const std::string& name){return resolve(root,name);};
        const auto parentPath=withinProject(document.parent);
        // Preserve dependencies referenced by the file. Only project-authored
        // sidecars attached to this source are excluded from the original view.
        const auto readAsset=[root,parentPath,originalModel](const std::string& path,std::string& bytes){
            if(originalModel && path.starts_with(parentPath)){
                const auto suffix=std::string_view(path).substr(parentPath.size());
                if(suffix==".batching.json"||suffix==".clips.json"||suffix==".wind.json"||suffix==".lmuv")return false;
            }
            const auto file=std::filesystem::weakly_canonical(path);
            const auto local=file.lexically_relative(root);
            if(local.empty()||local.is_absolute()||*local.begin()=="..")return false;
            std::ifstream input(file,std::ios::binary);
            if(!input)return false;
            bytes.assign(std::istreambuf_iterator<char>(input),{});return !input.bad();
        };
        if(retired_.size()>=8){
            retire();
            if(retired_.size()>=8)throw std::runtime_error("Waiting for prior preview frames");
        }
        retired_.reserve(retired_.size()+2);
        auto staged=std::make_unique<Session>();
        staged->provider=std::make_unique<render::ModelAssetProvider>(readAsset);
        // Share the Editor Device's explicit generation token. Pointer
        // identity alone is insufficient after graphics device recreation.
        staged->provider->setGpuDeviceLease(lease_);
        staged->registry=std::make_unique<Registry>();
        std::string error;
        try {
            staged->originalModel=originalModel;
            auto* parent=staged->provider->load(device_,parentPath);
            if(!parent)
                throw std::runtime_error("Parent Model import failed");
            Registry::Request parentRequest;
            parentRequest.model=parent;
            parentRequest.sources.push_back(parentPath);
            if(!staged->registry->publish("studio/attachments/parent",
                                           parentRequest,{}, {},&error))
                throw std::runtime_error("Parent appearance: "+error);
            staged->source=staged->registry->find("studio/attachments/parent");
            std::map<std::string,Registry::Snapshot> imported;
            size_t authoredIndex=0;
            for(const auto& child:document.attachments) {
                ++authoredIndex;
                if(!child.enabled)continue;
                const auto source=withinProject(child.model);
                if(child.animated) {
                    if(child.id.empty() || child.animationGraph.empty())
                        throw std::invalid_argument(
                            "animated child needs stable id and Graph file");
                    auto* model=staged->provider->load(device_,source);
                    if(!model)
                        throw std::runtime_error(
                            "animated child source Model import failed");
                    Registry::Request request;
                    request.model=model;
                    request.sources.push_back(source);
                    request.animationGraph=Json::string(
                        withinProject(child.animationGraph));
                    request.graphReader=readAsset;
                    request.sourceId=uint64_t(authoredIndex);
                    const auto key="studio/attachments/animated/"+child.id;
                    if(!staged->registry->publish(
                           key,request,{}, {},&error))
                        throw std::runtime_error(
                            "animated child Graph compilation: "+error);
                    auto appearance=staged->registry->find(key);
                    if(!appearance || !appearance->graph)
                        throw std::runtime_error(
                            "animated child published without Graph");
                    staged->animatedChildren.emplace(
                        child.id,std::move(appearance));
                    continue;
                }
                auto it=imported.find(source);
                if(it==imported.end()) {
                    Registry::Snapshot appearance;
                    Registry::Request request;
                    if(!Imported::importChild(
                        *staged->provider,device_,*staged->registry,
                        "studio/attachments/child/"+source,
                        source,request,appearance,{},&error))
                        throw std::runtime_error("Child appearance: "+error);
                    staged->children.push_back(appearance);
                    it=imported.emplace(source,std::move(appearance)).first;
                }
            }
            staged->parentEntity=staged->presentation.create();
            staged->attachmentHost=
                std::make_unique<view::SceneAttachmentHost>(
                    staged->presentation);
            const auto resolver=[&](const std::string& key)->Registry::Snapshot {
                const auto found=imported.find(withinProject(key));
                return found==imported.end()?Registry::Snapshot{}:found->second;
            };
            if(!staged->attachmentHost->activate(
                   {1,"preview"},staged->parentEntity,
                   authored,
                   staged->source,staged->provider->resources(),
                   resolver,&error))
                throw std::runtime_error("Scene attachment activate: "+error);

            Json result=Json::object();
            const auto* active=staged->attachmentHost->find(1,"preview");
            const size_t children = staged->animatedChildren.size() + (active ? active->bindings.children.size() : 0);
            result.set("children", Json::number(int64_t(children)));
            auto* model=staged->source->model;
            result.set("duration",Json::number(double(model->clips.empty()?0:model->clips.front().duration)));
            Json sockets=Json::array();for(const auto& node:model->nodes)if(!node.name.empty())sockets.push(Json::string(node.name));
            result.set("sockets",std::move(sockets));
            animatedSources_.clear();
            if(session_)retired_.push_back(std::move(session_));
            session_=std::move(staged);time_=0;last_={};
            return result;
        }catch(...){if(staged)retired_.push_back(std::move(staged));throw;}
    }
    bool ready()const{return bool(session_);}
    float time()const{return time_;}
    void controls(const Json& params){
        if(params.has("playing"))playing_=params.getBool("playing");
        if(params.has("time"))time_=float(std::clamp(params["time"].toDouble(),0.,86400.));
        if(params.has("yaw"))yaw_=float(params["yaw"].toDouble());
        if(params.has("pitch"))pitch_=float(std::clamp(params["pitch"].toDouble(),-.9,1.4));
        last_={};
    }
    void draw(int width,int height){
        if(!session_)return;
        const auto now=std::chrono::steady_clock::now();
        const float dt=playing_&&last_.time_since_epoch().count()?std::clamp(std::chrono::duration<float>(now-last_).count(),0.f,.1f):0.f;
        last_=now;
        const auto* model=session_->source->model;
        auto rigRead=session_->provider->resources().acquireModelRead(model);
        if(!rigRead)throw std::runtime_error("Parent model is unavailable");
        render::bindPose(*model,previewPose_);
        if(!session_->originalModel&&!model->clips.empty()){
            const float duration=model->clips.front().duration;
            if(duration>0){time_=dt?std::fmod(time_+dt,duration):std::min(time_,duration);render::sampleClip(model->clips.front(),time_,previewPose_);}
        }
        render::worldMatrices(*model,previewPose_,previewMatrices_);
        std::vector<view::AnimatedSceneAttachmentSources::Input> inputs;
        for(const auto& [id,appearance]:session_->animatedChildren)inputs.push_back({{1,"preview",id},appearance,appearance->parameters});
        view::AnimatedSceneAttachmentSources::Statistics stats;
        std::string error;
        if(!animatedSources_.sync(*session_->attachmentHost,session_->provider->resources(),inputs,dt,stats,&error))
            throw std::runtime_error(error);
        render::Camera cam;cam.target=(model->boundsMin+model->boundsMax)*.5f;
        cam.distance=std::max(.4f,render::length(model->boundsMax-model->boundsMin)*1.5f)*std::max(1.f,float(height)/width);
        cam.minDistance=.05f;cam.yaw=yaw_;cam.pitch=pitch_;
        renderer_.beginScene(cam,width,height);
        renderer_.lighting.skyColor={.8f,.85f,.9f};renderer_.lighting.fogDensity=0;
        presentationView_.beginPreparedFrame();animatedPreviewView_.beginPreparedFrame();
        Imported::Prepared packet;
        if(!session_->attachmentHost->prepare(1,"preview",session_->provider->resources(),render::mat4{},packet,&error))
            throw std::runtime_error(error);
        packet.object.id=1;packet.object.draw.pose=&previewPose_;packet.object.draw.nodeWorld=&previewMatrices_;
        presentationView_.submitOne(renderer_,packet.object);
        std::vector<view::AnimatedSceneAttachmentSources::Prepared> children(inputs.size());
        for(size_t i=0;i<inputs.size();++i){
            if(!animatedSources_.prepare(*session_->attachmentHost,session_->provider->resources(),inputs[i].key,
                    packet.object.draw,previewMatrices_,children[i],&error))throw std::runtime_error(error);
            animatedPreviewView_.submitOne(renderer_,children[i].object);
        }
        gfx::PassDesc pass;pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.135f;
        renderer_.render(pass);
    }
};
}
