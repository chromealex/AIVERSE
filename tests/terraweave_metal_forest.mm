// TerraWeave forest: production Renderer3D + gfx::Device Metal hardware
// capture. Tests shared Arbor BranchLOD, masked multi-view impostors, a
// resized offscreen target and GPU-idle model retirement. Not an ECS solver.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
namespace gfx=aiverse::gfx;
namespace render=aiverse::render;
namespace terra=aiverse::terraweave;
struct Target {
    int width=0,height=0;
    gfx::TextureHandle color{},depth{};
};
Target makeTarget(gfx::Device& device,int width,int height) {
    gfx::TextureDesc desc{};
    desc.width=width;desc.height=height;
    desc.renderTarget=true;desc.format=gfx::PixelFormat::RGBA8;
    desc.label="TerraWeave forest Metal scene color";
    const auto color=device.createTexture(desc);
    desc.format=gfx::PixelFormat::Depth;
    desc.label="TerraWeave forest Metal scene depth";
    return {width,height,color,device.createTexture(desc)};
}
bool valid(Target t) {return t.color.valid()&&t.depth.valid();}
void destroyTarget(gfx::Device& device,Target& t) {
    if(t.color.valid())device.destroyTexture(t.color);
    if(t.depth.valid())device.destroyTexture(t.depth);
    t={};
}
gfx::PassDesc pass(Target t) {
    gfx::PassDesc p{};
    p.colorTarget=t.color;p.depthTarget=t.depth;
    p.clearColor[0]=0.015f;
    p.clearColor[1]=0.019f;
    p.clearColor[2]=0.024f;
    p.clearColor[3]=1.0f;
    return p;
}
bool capture(const char* name,Target t,const std::vector<uint8_t>& rgba) {
    const char* prefix=std::getenv("AIVERSE_TERRAWEAVE_FOREST_CAPTURE");
    if(!prefix||!*prefix)return true;
    if(rgba.size()!=size_t(t.width)*t.height*4u)return false;
    const std::string file=std::string(prefix)+"-"+name+".ppm";
    std::FILE* f=std::fopen(file.c_str(),"wb");
    if(!f)return false;
    std::fprintf(f,"P6\n%d %d\n255\n",t.width,t.height);
    bool ok=true;
    for(size_t i=0;i<rgba.size();i+=4u) {
        if(std::fwrite(rgba.data()+i,1,3,f)!=3) {ok=false;break;}
    }
    if(std::fclose(f)!=0)ok=false;
    if(ok)std::printf("[terraweave-metal-forest] capture=%s\n",file.c_str());
    return ok;
}
struct Difference {
    int changed=0;
    int left=0,right=0,top=0,bottom=0;
    int holes=0;
};
Difference diff(const std::vector<uint8_t>& empty,
                const std::vector<uint8_t>& forest,Target t) {
    Difference d{};
    d.left=t.width;d.top=t.height;
    if(empty.size()!=forest.size() ||
       empty.size()!=size_t(t.width)*t.height*4u)return d;
    for(int y=0;y<t.height;++y)for(int x=0;x<t.width;++x) {
        const size_t k=(size_t(y)*t.width+x)*4u;
        const int delta=std::max({
            std::abs(int(empty[k])-int(forest[k])),
            std::abs(int(empty[k+1])-int(forest[k+1])),
            std::abs(int(empty[k+2])-int(forest[k+2]))});
        if(delta>16) {
            ++d.changed;
            d.left=std::min(d.left,x);d.right=std::max(d.right,x);
            d.top=std::min(d.top,y);d.bottom=std::max(d.bottom,y);
        }
    }
    if(!d.changed)return d;
    for(int y=d.top;y<=d.bottom;++y)for(int x=d.left;x<=d.right;++x) {
        const size_t k=(size_t(y)*t.width+x)*4u;
        const int delta=std::max({
            std::abs(int(empty[k])-int(forest[k])),
            std::abs(int(empty[k+1])-int(forest[k+1])),
            std::abs(int(empty[k+2])-int(forest[k+2]))});
        if(delta<=16)++d.holes;
    }
    return d;
}
}

int aiverseMain(int,char**) {
    @autoreleasepool {
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        // This acceptance target requires Metal hardware: a CTest SKIP may
        // never be misreported as proof of forest rasterization.
        if(!metal) {
            std::fputs("FAIL [terraweave-metal-forest] no Metal GPU\n",stderr);
            return 1;
        }
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.frame=CGRectMake(0,0,256,256);
        layer.drawableSize=CGSizeMake(256,256);
        auto device=gfx::createMetalDevice((__bridge void*)layer);
        if(!device) {
            std::fputs("FAIL [terraweave-metal-forest] no production Metal RHI\n",stderr);
            return 1;
        }
        int failures=0;
        const auto check=[&](bool yes,const char* reason) {
            if(!yes) {
                std::fprintf(stderr,"FAIL [terraweave-metal-forest] %s\n",reason);
                ++failures;
            }
        };
        render::Renderer3D renderer;
        if(!renderer.init(*device)) {
            std::fprintf(stderr,"FAIL [terraweave-metal-forest] Renderer3D init: %s\n",
                         device->lastError().c_str());
            return 1;
        }
        renderer.setShadowQuality(render::ShadowQuality::Off);
        renderer.setModelReflectionsEnabled(false);
        renderer.setTraceEnabled(false);
        renderer.lights.setQuality(render::LightQuality::Off);
        renderer.lighting.fogDensity=0;

        terra::ArborForestAdapter forest;
        terra::TreeProfile profile;
        profile.speciesId=77001;
        profile.primaryBranches=12;
        profile.minHeightCm=950;
        profile.maxHeightCm=1050;
        profile.leafRadiusCm=95;
        forest.addPrototype(profile,0x517eeda1u);
        const auto* nearModel=forest.prototype(profile.speciesId);
        const auto* farModel=forest.billboardPrototype(profile.speciesId);
        check(nearModel && farModel && nearModel!=farModel,
              "shared BranchLOD and impostor prototype missing");
        check(farModel && farModel->materials.size()==2 &&
              farModel->materials[0].alpha==render::AlphaMode::Mask &&
              farModel->materials[1].alpha==render::AlphaMode::Mask,
              "billboard bark/foliage must both be alpha-masked");
        if(farModel && !farModel->images.empty()) {
            size_t clear=0,solid=0;
            for(size_t i=3;i<farModel->images[0].rgba.size();i+=4) {
                clear+=farModel->images[0].rgba[i]==0;
                solid+=farModel->images[0].rgba[i]==255;
            }
            check(clear>100 && solid>100,
                  "impostor atlas lacks real transparent and opaque texels");
        }
        check(forest.billboardAzimuthCount(profile.speciesId)==8,
              "expected eight real impostor azimuth slices");

        terra::FoliageInstance instance;
        instance.key=0x1234;instance.speciesId=profile.speciesId;
        instance.scalePermille=1000;
        const std::vector<terra::FloraBatch> nearBatches{
            {profile.speciesId,terra::TreeLOD::High,{instance}}};
        const std::vector<terra::FloraBatch> farBatches{
            {profile.speciesId,terra::TreeLOD::Impostor,{instance}}};
        // Independently prove that the actual Flora distance policy selects
        // both geometry and impostor tiers. The GPU scenes below keep the
        // same camera distance to compare visible representations fairly.
        terra::FloraView nearPolicy;
        nearPolicy.farDensityPermille=1000;
        const auto selectedNear=terra::buildFloraBatches({instance},nearPolicy);
        auto farPolicy=nearPolicy;
        farPolicy.cameraZcm=24000;
        const auto selectedFar=terra::buildFloraBatches({instance},farPolicy);
        check(selectedNear.size()==1 &&
              selectedNear[0].lod==terra::TreeLOD::High &&
              selectedFar.size()==1 &&
              selectedFar[0].lod==terra::TreeLOD::Impostor,
              "Flora LOD policy failed to switch near tree to impostor");
        if(!forest.uploadModels(*device) ||
           !nearModel->vb.valid() || !farModel->vb.valid()) {
            std::fprintf(stderr,"FAIL [terraweave-metal-forest] Arbor model upload: %s\n",
                         device->lastError().c_str());
            renderer.shutdown();
            return 1;
        }

        Target emptyA=makeTarget(*device,256,256);
        Target nearA=makeTarget(*device,256,256);
        Target farA=makeTarget(*device,256,256);
        Target farB=makeTarget(*device,256,256);
        Target emptyResized=makeTarget(*device,192,128);
        Target nearResized=makeTarget(*device,192,128);
        if(!valid(emptyA)||!valid(nearA)||!valid(farA)||
           !valid(farB)||!valid(emptyResized)||!valid(nearResized)) {
            std::fputs("FAIL [terraweave-metal-forest] render target allocation\n",stderr);
            return 1;
        }
        render::RTSCamera cameraA;
        cameraA.target={0,5,0};
        cameraA.distance=27;
        cameraA.yaw=0.12f;
        cameraA.pitch=0.15f;
        cameraA.fovY=0.65f;
        cameraA.zNear=0.1f;cameraA.zFar=200;
        auto cameraB=cameraA;
        cameraB.yaw=1.65f;

        std::vector<float> azimuths;
        const auto draw=[&](const render::RTSCamera& camera,Target target,
                             const std::vector<terra::FloraBatch>* batches) {
            renderer.beginScene(camera,target.width,target.height);
            if(batches==&farBatches) {
                // Query material overrides from the SAME live camera and
                // frame used for the real Renderer3D submission below.
                forest.forEachDraw(*batches,{},[&](const render::ModelDraw& d) {
                    check(d.materials && d.materials->size()==2,
                          "impostor material overrides missing");
                    if(d.materials)azimuths.push_back(
                        (*d.materials)[0].uvOffset.x);
                },terra::ForestCameraCm{
                    int64_t(std::llround(renderer.cameraPos().x*100.0f)),
                    int64_t(std::llround(renderer.cameraPos().z*100.0f))});
            }
            if(batches)forest.submit(renderer,*batches);
            renderer.render(pass(target));
            device->endPass();
        };
        device->beginFrame(256,256);
        draw(cameraA,emptyA,nullptr);
        draw(cameraA,nearA,&nearBatches);
        draw(cameraA,farA,&farBatches);
        draw(cameraB,farB,&farBatches);
        draw(cameraA,emptyResized,nullptr);
        draw(cameraA,nearResized,&nearBatches);
        device->endFrame();

        std::vector<uint8_t> p0,p1,p2,p3,p4,p5;
        check(device->readColorRGBA8(emptyA.color,p0) &&
              device->readColorRGBA8(nearA.color,p1) &&
              device->readColorRGBA8(farA.color,p2) &&
              device->readColorRGBA8(farB.color,p3) &&
              device->readColorRGBA8(emptyResized.color,p4) &&
              device->readColorRGBA8(nearResized.color,p5),
              "real offscreen Metal color readback failed");
        const auto near=diff(p0,p1,nearA);
        const auto far=diff(p0,p2,farA);
        const auto resized=diff(p4,p5,nearResized);
        check(near.changed>100,
              "BranchLOD did not render visible Metal pixels");
        check(far.changed>60,
              "masked impostor did not render visible Metal pixels");
        check(far.holes>16,
              "impostor RGB mask appears to fill its entire silhouette box");
        check(resized.changed>30,
              "resized offscreen forest RT lost visible geometry");

        check(azimuths.size()==2 && azimuths[0]!=azimuths[1],
              "impostor azimuth selection ignored camera orientation");

        check(capture("baseline",emptyA,p0) &&
              capture("branch-lod",nearA,p1) &&
              capture("impostor-a",farA,p2) &&
              capture("impostor-b",farB,p3) &&
              capture("baseline-resized",emptyResized,p4) &&
              capture("branch-resized",nearResized,p5),
              "cannot persist actual Metal P6 forest evidence");
        std::printf("[terraweave-metal-forest] near=%d far=%d holes=%d resized=%d "
                    "azimuthA=%.5f azimuthB=%.5f\n",
                    near.changed,far.changed,far.holes,resized.changed,
                    azimuths.empty()?0.0f:azimuths[0],
                    azimuths.size()<2?0.0f:azimuths[1]);

        // Finish *submitted* GPU work before releasing shared Atlas textures,
        // Model geometry, Renderer3D shaders and pending offscreen RTs.
        if(!device->waitForResourceIdle()) {
            std::fputs("FAIL [terraweave-metal-forest] GPU idle barrier failed\n",stderr);
            return 1;
        }
        forest.releaseModels(*device);
        renderer.shutdown();
        for(Target* t:{&emptyA,&nearA,&farA,&farB,&emptyResized,&nearResized})
            destroyTarget(*device,*t);
        std::printf("[terraweave-metal-forest] failures=%d\n",failures);
        if(failures)return 1;
        std::puts("PASS [terraweave-metal-forest] real Metal BranchLOD, masked "
                  "8-view impostor, RT resize and GPU-idle retirement");
        return 0;
    }
}
