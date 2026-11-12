#include "SurfaceFixture.h"
#include "tools/modeling/SurfaceProxy.h"
#include "engine/render/Renderer3D.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
int aiverseMain(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"plane";const float yaw=argc>2?std::strtof(argv[2],nullptr):0;
    auto source=modeling::build(proxytest::wallDocument(mode=="hole"));
    auto options=Json::parse(R"({"mode":"plane","resolution":128,"axis":"z"})");if(mode=="box")options.set("mode",Json::string("box"));
    auto baked=modeling::bakeSurfaceProxy(*source.model,options);
    // Exercise the exported asset and the runtime importer, not only the in-memory bake.
    const auto bytes=modeling::exportGlb(*baked.model);auto proxy=render::loadModel("proxy.glb",[&](const std::string&,std::string& b){b=bytes;return true;});
    std::printf("%s\n",baked.report.dump().c_str());
    render::Renderer3D renderer;gfx::Device* device=nullptr;
    platform::AppConfig cfg;cfg.title="AIVERSE Surface Proxy: source left, proxy right";cfg.width=1200;cfg.height=720;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& dev){device=&dev;renderer.init(dev);renderer.setShadowQuality(render::ShadowQuality::Off);render::uploadModel(dev,*source.model);render::uploadModel(dev,*proxy);};
    cb.frame=[&](gfx::Device& dev,double){render::RTSCamera cam;cam.distance=15;cam.target={0,0,0};cam.yaw=0;cam.pitch=0;
        renderer.beginScene(cam,dev.framebufferWidth(),dev.framebufferHeight());
        render::ModelDraw d;d.model=source.model.get();d.world=render::mat4::translate({-2.6f,0,0})*render::mat4::rotateY(yaw);renderer.drawModel(d);
        d.model=proxy.get();d.world=render::mat4::translate({2.6f,0,0})*render::mat4::rotateY(yaw);renderer.drawModel(d);
        gfx::PassDesc pass;pass.clearColor[0]=.08f;pass.clearColor[1]=.1f;pass.clearColor[2]=.14f;renderer.render(pass);
    };
    cb.shutdown=[&]{render::releaseModel(*device,*source.model);render::releaseModel(*device,*proxy);renderer.shutdown();};
    return platform::run(cfg,cb);
}
