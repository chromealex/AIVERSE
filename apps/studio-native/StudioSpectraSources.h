// Native Studio adapter: authored flat-map sources -> existing ECS presenter.
// Device and frame ownership remain with StudioScene.
#pragma once
#include "libraries/include/aiverselib/mechanics/SpectraSourceMirror.h"
#include "engine/fx/SpectraFX.h"
#include "engine/fx/FxModelMesh.h"
#include "engine/render/TextureCache.h"
#include "apps/studio-native/StudioDocumentIds.h"
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
        presenter_.reset();mirror_.clear();
        particles_.shutdown();textures_.shutdown();
        definitions_.clear();project_.clear();mapPath_.clear();error_.clear();
        revision_=UINT64_MAX;ready_=false;
    }
    bool animated() const{
        return ready_&&(mirror_.size()!=0||particles_.stats().particles>0);
    }
    const std::string& error()const{return error_;}
    void synchronize(gfx::Device& device,const editor::MapDoc& doc,const std::string& project,
                     const std::set<std::string>& hidden){
        if(project!=project_||doc.path!=mapPath_){
            device.waitForResourceIdle();shutdown();
            project_=project;mapPath_=doc.path;
        }
        if(project.empty()||doc.data.worldSpace||doc.data.planet())return;
        // Only rebuild source declarations on a document revision or visibility change.
        if(revision_==doc.revision&&hidden_==hidden)return;
        revision_=doc.revision;hidden_=hidden;
        std::vector<lib::mechanics::SpectraSourceSnapshot> rows;
        std::set<std::string> activeNames,seen;
        error_.clear();
        for(size_t i=0;i<doc.data.entities.size()&&rows.size()<2048;++i){
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
                activeNames.insert(source.effect);
                const auto transform=editor::authoredLocalTransform(entity);
                const auto p=transform.position;
                rows.push_back({identity,source,{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())},{0,1,0}});
            }catch(const std::exception& e){error_=e.what();}
        }
        mirror_.sync(rows);
        // Retain loaded definitions while their detached particles fade.
        // Definition count is bounded for this document session.
    }
    void prepare(const fx::SpectraFxView& view,float dt){
        if(!ready_||!view.valid())return;
        particles_.setView(view);
        presenter_->sync(mirror_.world(),lib::mechanics::SpectraSourceMirror::resolve);
        particles_.update(dt);
    }
    void draw(){if(ready_)particles_.render({});}
private:
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
        presenter_=std::make_unique<lib::mechanics::SpectraPresentationSystem>(particles_);
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
        if(definitions_.size()>=128){error_="SpectraFX scene definition limit reached";return false;}
        std::string text;
        if(!read("fx/graphs/"+name+".vfx.json",text)||text.size()>512*1024){
            error_="Missing or oversized SpectraFX graph: "+name;return false;
        }
        const auto graph=Json::parse(text);
        fx::EffectDef def;std::vector<std::string> errors;
        if(graph.getInt("version")!=2||!spectra::compileGraph(graph,def,errors)){
            error_=errors.empty()?"Invalid SpectraFX graph: "+name:errors.front();return false;
        }
        if(particles_.define(name,def)<0){error_="Cannot define SpectraFX graph: "+name;return false;}
        definitions_.insert(name);return true;
    }
    render::TextureCache textures_;
    fx::ParticleSystem particles_;
    lib::mechanics::SpectraSourceMirror mirror_;
    std::unique_ptr<lib::mechanics::SpectraPresentationSystem> presenter_;
    std::set<std::string> definitions_,hidden_;
    std::string project_,mapPath_,error_;
    uint64_t revision_=UINT64_MAX;
    bool ready_=false;
};
}
