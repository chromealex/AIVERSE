#pragma once
#include "engine/fx/FxModelMesh.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraSdfFileIO.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TextureCache.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace aiverse::studio {

class StudioPreviewParticles {
protected:
    gfx::Device& device_;
    std::filesystem::path project_;
    render::TextureCache textures_;
    fx::ParticleSystem particles_;
    std::filesystem::path asset(const std::string& name) const {
        namespace fs=std::filesystem;
        if(name.empty() || fs::path(name).is_absolute())return {};
        std::error_code ec;
        const auto path=fs::weakly_canonical(project_/name,ec);
        const auto local=path.lexically_relative(project_);
        if(ec || local.empty() || *local.begin()==".." || !fs::is_regular_file(path,ec))return {};
        return path;
    }
    fx::FileReader read;
public:
    StudioPreviewParticles(gfx::Device& device,render::RenderDeviceLifetime::Lease lease,const std::string& project)
        :device_(device),project_(std::filesystem::canonical(project)){
        read=[this](const std::string& name,std::string& out){
            std::ifstream file(asset(name),std::ios::binary);
            if(!file)return false;
            out.assign(std::istreambuf_iterator<char>(file),{});
            return !file.bad();
        };
        textures_.init(device);
        particles_.setFileReader(read);
        particles_.setMeshLoader([this](const auto& name,auto& out){return fx::meshFromModelFile(name,read,out);});
        particles_.setTextureLoader([this](const std::string& name,fx::FxTexture& out){
            if(asset(name).empty())return false;
            const auto ref=textures_.resolve(name,false,project_.string()+"/");
            if(!ref.valid())return false;
            out={ref.tex,ref.u0,ref.v0,ref.u1,ref.v1};return true;
        });
        particles_.setGpuDeviceLease(std::move(lease));
        if(!particles_.init(&device))throw std::runtime_error(particles_.lastError());
        particles_.setQuality(fx::FxQuality::High);

    }
    ~StudioPreviewParticles(){particles_.shutdown();textures_.shutdown();}

};
}
