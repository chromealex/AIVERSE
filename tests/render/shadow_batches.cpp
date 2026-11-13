// Real-backend A/B: the same model/material copies, camera and shadow map, with batching off/on.
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelMerge.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
using namespace aiverse;
int aiverseMain(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: shadow_batches MODEL OUTPUT_PREFIX\n"); return 2; }
    auto read = [](const std::string& path, std::string& bytes) {
        std::ifstream f(path, std::ios::binary); if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), {}); return true;
    };
    if (std::string(argv[1]) == "--audit") {
        size_t models=0, before=0, after=0, addedIndices=0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            if (entry.path().extension() != ".glb") continue;
            auto m=render::loadModel(entry.path().string(), read);
            size_t oldSize=m->indices.size();
            render::prepareShadowBatches(*m); addedIndices+=m->indices.size()-oldSize;
            for (const auto& n:m->nodes) if(n.mesh>=0) {
                const auto& mesh=m->meshes[size_t(n.mesh)];
                for(const auto& p:mesh.primitives) {
                    const auto& mat=m->materials[size_t(p.material)];
                    if(mat.castShadow && mat.alpha!=render::AlphaMode::Blend && !mat.transparent) {++before;if(!p.shadowBatched)++after;}
                }
                after+=mesh.shadowBatch.indexCount>0;
            }
            ++models;
        }
        std::printf("models=%zu shadow_before=%zu shadow_after=%zu added_indices=%zu\n",models,before,after,addedIndices);
        return models && after<before ? 0 : 1;
    }
    auto model=render::loadModel(argv[1],read);
    const std::string prefix = argv[2];
    const bool reflection=std::getenv("AIVERSE_SHADOW_TEST_REFLECTION")!=nullptr;
    render::Renderer3D renderer; render::Mesh floor; gfx::Device* device = nullptr;
    std::vector<render::Material> materials;
    std::vector<uint32_t> batchCounts;
    std::vector<uint8_t> before;
    uint32_t shadowBefore=0, opaqueBefore=0; int frame=0; bool failed=false, finished=false;
    platform::AppConfig cfg; cfg.title="Shadow batching A/B"; cfg.width=960; cfg.height=640; cfg.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        device=&d; if (!renderer.init(d) || !render::uploadModel(d,*model)) { failed=true; return; }
        renderer.setProfiling(true); renderer.setShadowQuality(render::ShadowQuality::High);
        renderer.lighting.fogDensity=0;
        renderer.lights.setQuality(render::LightQuality::High);
        floor=render::MeshBuilder().box({0,-.1f,0},{60,.2f,60}).upload(d);
        materials=model->materials; // Match RtsView's per-type material-copy path.
        for (const auto& mesh:model->meshes) batchCounts.push_back(mesh.shadowBatch.indexCount);
    };
    cb.frame=[&](gfx::Device& d,double) {
        if(failed||finished||frame>40){platform::requestQuit();return;}
        const bool batch=frame>=15;
        for(size_t i=0;i<model->meshes.size();++i) model->meshes[i].shadowBatch.indexCount=batch?batchCounts[i]:0;
        render::RTSCamera cam; cam.target={0,2,0};cam.distance=std::getenv("AIVERSE_SHADOW_TEST_DISTANCE")?float(std::atof(std::getenv("AIVERSE_SHADOW_TEST_DISTANCE"))):14;cam.pitch=.85f;cam.yaw=.45f;
        renderer.beginScene(cam,960,640);
        renderer.drawMesh(floor,{}, {.6f,.6f,.6f,1});
        if(reflection) renderer.drawMesh(floor,render::mat4::translate({0,.2f,0}),{.1f,.2f,.3f,.6f},render::kDrawNoCastShadow,.9f);
        render::ModelDraw draw;draw.model=model.get();draw.materials=&materials;
        const auto center=(model->boundsMin+model->boundsMax)*.5f;
        draw.world=render::mat4::scale({.25f,.25f,.25f})*render::mat4::translate({-center.x,-model->boundsMin.y,-center.z});
        renderer.drawModel(draw);gfx::PassDesc pass;renderer.render(pass);
        if(frame==8||frame==23){
            const auto& p=renderer.profile();
            std::printf("batch=%d shadow=%u opaque=%u total=%u error=%s\n",batch,p.draws[0],p.draws[1],renderer.drawCalls(),renderer.modelError().c_str());
            if(!batch){shadowBefore=p.draws[0];opaqueBefore=p.draws[1];}
            else if(p.draws[0]>=shadowBefore||p.draws[1]!=opaqueBefore) failed=true;
            d.captureNextFrame([&,batch](int w,int h,std::vector<uint8_t> px){
                if(w!=960||h!=640||px.size()!=size_t(w*h*4)){failed=true;return;}
                std::ofstream f(prefix+(batch?"-after.ppm":"-before.ppm"),std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4) f.write(reinterpret_cast<const char*>(px.data()+i),3);
                if(!f)failed=true;
                if(!batch)before=std::move(px);
                else {size_t changed=0;for(size_t i=0;i<px.size();++i)changed+=i>=before.size()||px[i]!=before[i];
                    std::printf("changed_channels=%zu\n",changed);failed|=changed!=0;finished=true;}
            });
        }
        failed|=!renderer.modelError().empty();d.endPass();++frame;
    };
    cb.shutdown=[&]{device->destroyBuffer(floor.vb);device->destroyBuffer(floor.ib);render::releaseModel(*device,*model);renderer.shutdown();};
    platform::run(cfg,cb);return failed||!finished?1:0;
}
