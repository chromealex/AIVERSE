#include "SurfaceFixture.h"
#include "engine/render/Renderer3D.h"
#include "engine/platform/Platform.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
using namespace aiverse;
int aiverseMain(int argc,char** argv){
    const unsigned limit=argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):0;
    auto model=modeling::build(proxytest::wallDocument(false));render::Renderer3D renderer;gfx::Device* device=nullptr;
    platform::AppConfig cfg;cfg.title="AIVERSE instancing validation";cfg.width=1280;cfg.height=720;
    platform::AppCallbacks cb;int frame=0;std::vector<double> gpu,cpu;
    cb.init=[&](gfx::Device& dev){device=&dev;renderer.init(dev);renderer.setInstanceLimit(limit);renderer.setShadowQuality(render::ShadowQuality::Off);render::uploadModel(dev,*model.model);};
    cb.frame=[&](gfx::Device& dev,double){
        for(auto t:dev.takeGpuTimings())if(t.valid)gpu.push_back(t.milliseconds);
        auto start=std::chrono::steady_clock::now();render::RTSCamera cam;cam.distance=100;cam.pitch=0;cam.target={0,0,0};renderer.beginScene(cam,1280,720);
        for(int i=0;i<1025;++i){render::ModelDraw d;d.model=model.model.get();d.world=render::mat4::translate({float(i%41-20)*2.2f,float(i/41-12)*2.2f,0})*render::mat4::scale({.4f,.4f,.4f});d.tint={.4f+float(i%3)*.3f,.4f+float(i%5)*.15f,.4f+float(i%7)*.1f,1};renderer.drawModel(d);}
        gfx::PassDesc pass;renderer.render(pass);
        if(frame>=60&&frame<180){dev.timeGpuFrame(unsigned(frame));cpu.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        if(frame==190){std::sort(gpu.begin(),gpu.end());std::sort(cpu.begin(),cpu.end());std::printf("limit=%u draws=%u gpuSamples=%zu gpuMedian=%.6f cpuMedian=%.6f error=%s\n",renderer.instanceLimit(),renderer.drawCalls(),gpu.size(),gpu.empty()?0:gpu[gpu.size()/2],cpu[cpu.size()/2],renderer.modelError().c_str());}
        ++frame;
    };
    cb.shutdown=[&]{render::releaseModel(*device,*model.model);renderer.shutdown();};return platform::run(cfg,cb);
}
