// Real-backend A/B: the same model/material copies, camera and shadow map, with batching off/on.
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelMerge.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <algorithm>
using namespace aiverse;
// GPU-free invariants, including shared vertices and material overrides.
static bool unitChecks() {
    using namespace render;
    int checks=0;
    auto check=[&](bool ok){++checks;if(!ok)std::fprintf(stderr,"material palette check %d failed\n",checks);return ok;};
    Model m; m.materials.resize(3); m.vertices.resize(3); m.indices={0,1,2,0,1,2,0,1,2};
    m.materials[0].baseColor={.137f,.271f,.819f,1};
    m.materials[1].metallic=.731f; m.materials[1].roughness=.213f;
    m.materials[2].baseTex=1;
    MeshDef mesh;
    for(int i=0;i<3;++i){Primitive p;p.firstIndex=uint32_t(i*3);p.indexCount=3;p.material=i;mesh.primitives.push_back(p);}
    m.meshes.push_back(mesh);
    const auto indices=m.indices;
    bool ok=check(prepareMaterialBatches(m)==2);
    ok&=check(m.meshes[0].materialBatches.size()==1);
    const auto& batch=m.meshes[0].materialBatches[0];
    ok&=check(batch.primitive.indexCount==6 && batch.parts==std::vector<size_t>{0,1});
    ok&=check(m.vertices.size()==6 && std::equal(indices.begin(),indices.end(),m.indices.begin()));
    for(uint32_t i=0;i<6;++i)ok&=check(m.vertices[m.indices[batch.primitive.firstIndex+i]].weights[0]==i/3);
    ok&=check(prepareMaterialBatches(m)==0);
    auto mats=m.materials;
    mats[0].baseColor.x=.999123f;mats[1].roughness=.456789f;
    ok&=check(materialBatchCompatible(m.meshes[0],batch,mats));
    mats[1].normalTex=0;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].alpha=AlphaMode::Blend;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].receiveShadow=false;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats.resize(kMaxMaterialPalette+1);ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));
    ok&=check(m.materials[0].baseColor.x==.137f && m.materials[1].roughness==.213f);
    auto invalid=[&](int mode){
        Model x;x.vertices.resize(3);x.indices=indices;x.materials.resize(3);x.meshes.push_back(mesh);
        if(mode==0)x.meshes[0].primitives[0].morphTargets=1;
        if(mode==1)x.skins.resize(1);
        if(mode==2)x.variants.push_back("variant");
        if(mode==3)x.materials.resize(kMaxMaterialPalette+1);
        return prepareMaterialBatches(x)==0;
    };
    for(int i=0;i<4;++i)ok&=check(invalid(i));
    Model lod;lod.vertices.resize(3);lod.indices=indices;lod.materials.resize(3);lod.meshes={mesh,mesh};
    lod.meshes[0].lods.push_back({1,20});
    ok&=check(prepareMaterialBatches(lod)==6 && lod.meshes[0].materialBatches.size()==1 && lod.meshes[1].materialBatches.size()==1);
    std::printf("material palette CPU checks=%d result=%s\n",checks,ok?"passed":"FAILED");
    return ok;
}

int aiverseMain(int argc, char** argv) {
    if (!unitChecks()) return 1;
    if (argc == 2 && std::string(argv[1]) == "--unit") return 0;
    if (argc != 3) { std::fprintf(stderr, "usage: material_batches MODEL OUTPUT_PREFIX\n"); return 2; }
    auto read = [](const std::string& path, std::string& bytes) {
        std::ifstream f(path, std::ios::binary); if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), {}); return true;
    };
    if (std::string(argv[1]) == "--audit") {
        size_t models=0, before=0, after=0, addedIndices=0, addedVertices=0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            if (entry.path().extension() != ".glb") continue;
            auto m=render::loadModel(entry.path().string(), read);
            const size_t ni=m->indices.size(), nv=m->vertices.size();
            render::prepareMaterialBatches(*m);
            addedIndices+=m->indices.size()-ni; addedVertices+=m->vertices.size()-nv;
            for (const auto& n:m->nodes) if(n.mesh>=0) {
                const auto& mesh=m->meshes[size_t(n.mesh)];
                before+=mesh.primitives.size();after+=mesh.primitives.size();
                for(const auto& b:mesh.materialBatches)after-=b.parts.size()-1;
            }
            ++models;
        }
        std::printf("models=%zu opaque_before=%zu opaque_after=%zu added_indices=%zu added_vertices=%zu\n",models,before,after,addedIndices,addedVertices);
        return models && after<before ? 0 : 1;
    }
    auto model=render::loadModel(argv[1],read);
    const std::string prefix = argv[2];
    const std::string scenario = std::getenv("AIVERSE_PALETTE_TEST_CASE") ? std::getenv("AIVERSE_PALETTE_TEST_CASE") : "default";
    const bool reflection=std::getenv("AIVERSE_SHADOW_TEST_REFLECTION")!=nullptr;
    render::Renderer3D renderer; render::Mesh floor; gfx::Device* device = nullptr;
    std::vector<render::Material> materials;
    std::vector<std::vector<render::MaterialBatch>> batches;
    std::vector<uint8_t> before;
    uint32_t shadowBefore=0, opaqueBefore=0; int frame=0; bool failed=false, finished=false;
    platform::AppConfig cfg; cfg.title="Material palette A/B"; cfg.width=960; cfg.height=640; cfg.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        device=&d; if (!renderer.init(d) || !render::uploadModel(d,*model)) { failed=true; return; }
        renderer.setProfiling(true); renderer.setShadowQuality(render::ShadowQuality::High);
        renderer.lighting.fogDensity=0;
        if (const char* q=std::getenv("AIVERSE_PALETTE_TEST_QUALITY")) renderer.setMaterialQuality(render::MaterialQuality(std::atoi(q)));
        renderer.lights.setQuality(render::LightQuality::High);
        floor=render::MeshBuilder().box({0,-.1f,0},{60,.2f,60}).upload(d);
        materials=model->materials; // Match RtsView's per-type material-copy path.
        if (scenario=="overrides") for(size_t i=0;i<materials.size();++i) {
            materials[i].baseColor.x*=.731234f; materials[i].metallic=float(i%3)*.27913f;
            materials[i].roughness=.1213f+float(i%4)*.174321f;
        }
        if (scenario=="fallback") for(size_t i=0;i<materials.size();++i) materials[i].uvOffset.x=float(i)*.137f;
        if (scenario=="lod1" || scenario=="lod2") for(auto& n:model->nodes) if(n.mesh>=0) {
            const auto& lods=model->meshes[size_t(n.mesh)].lods;
            if(!lods.empty()) n.mesh=lods[scenario=="lod2"?lods.size()-1:0].mesh;
        }
        for (const auto& mesh:model->meshes) batches.push_back(mesh.materialBatches);
    };
    cb.frame=[&](gfx::Device& d,double) {
        if(failed||finished||frame>40){platform::requestQuit();return;}
        const bool batch=frame>=15;
        for(size_t i=0;i<model->meshes.size();++i) model->meshes[i].materialBatches=batch?batches[i]:std::vector<render::MaterialBatch>{};
        render::RTSCamera cam; cam.target={0,2,0};cam.distance=std::getenv("AIVERSE_SHADOW_TEST_DISTANCE")?float(std::atof(std::getenv("AIVERSE_SHADOW_TEST_DISTANCE"))):14;cam.pitch=.85f;cam.yaw=.45f;
        renderer.beginScene(cam,960,640);
        renderer.drawMesh(floor,{}, {.6f,.6f,.6f,1});
        if(reflection) renderer.drawMesh(floor,render::mat4::translate({0,.2f,0}),{.1f,.2f,.3f,.6f},render::kDrawNoCastShadow,.9f);
        render::ModelDraw draw;draw.model=model.get();draw.materials=&materials;
        const auto center=(model->boundsMin+model->boundsMax)*.5f;
        draw.world=render::mat4::scale({.25f,.25f,.25f})*render::mat4::translate({-center.x,-model->boundsMin.y,-center.z});
        if (scenario=="effects") {draw.dissolve=.18f;draw.outline={1,.2f,0,.025f};}
        if (scenario=="transition") for(size_t i=0;i<model->nodes.size();++i) {
            const auto& n=model->nodes[i];if(n.mesh<0)continue;
            auto& mesh=model->meshes[size_t(n.mesh)];if(mesh.lods.empty())continue;
            const auto xf=draw.world*model->bindWorld[i];const float error=render::length(mesh.boundsMax-mesh.boundsMin);
            float pixels=render::geometryErrorPixels(renderer.viewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,960,640,d.info().clipDepthZeroToOne);
            pixels=std::max(pixels,render::geometryErrorPixels(renderer.lightViewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,renderer.shadowMapSize(),renderer.shadowMapSize(),d.info().clipDepthZeroToOne));
            mesh.lods.resize(1);mesh.lods[0].maxPixels=pixels;renderer.setModelLodFadeWidth(.15f);
        }
        renderer.drawModel(draw);
        if(scenario=="instanced"){draw.world=render::mat4::translate({3,0,0})*draw.world;renderer.drawModel(draw);}
        gfx::PassDesc pass;renderer.render(pass);
        if(frame==8||frame==23){
            const auto& p=renderer.profile();
            std::printf("batch=%d shadow=%u opaque=%u total=%u error=%s\n",batch,p.draws[0],p.draws[1],renderer.drawCalls(),renderer.modelError().c_str());
            if(!batch){shadowBefore=p.draws[0];opaqueBefore=p.draws[1];}
            else if(p.draws[0]!=shadowBefore || (scenario=="fallback"?p.draws[1]!=opaqueBefore:p.draws[1]>=opaqueBefore)) failed=true;
            if(scenario=="transition") {std::printf("lod_transition_nodes=%u\n",p.lodTransitionNodes);failed|=p.lodTransitionNodes==0;}
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
