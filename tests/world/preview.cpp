#include "engine/world/WorldScene.h"
#include "engine/platform/Platform.h"
#include "engine/platform/ContentReader.h"
#include <cstdio>
using namespace aiverse;
int aiverseMain(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:"examples/world/streamed";
        auto reader=platform::makeContentReader(root);std::string text;
        if(!reader("map.json",text))return 2;
        world::WorldScene scene(Json::parse(text)["world"],reader);
        render::Renderer3D renderer;gfx::Device* device=nullptr;int frame=0;bool failed=false;
        platform::AppConfig config;config.title="AIVERSE Async World Verification";config.width=960;config.height=640;
        platform::AppCallbacks callbacks;
        callbacks.init=[&](gfx::Device& d){device=&d;renderer.init(d);renderer.setShadowQuality(render::ShadowQuality::Low);};
        callbacks.frame=[&](gfx::Device& d,double){
            const bool far=frame>=100&&frame<200,empty=frame>=300&&frame<330;
            Vec3 position=far?Vec3{420_fx,{},420_fx}:Vec3{32_fx,{},32_fx};
            scene.update(d,empty?std::span<const Vec3>{}:std::span<const Vec3>{&position,1},0);
            if(frame==99||frame==199||frame==299||frame==399){const auto& s=scene.status();
                failed|=!s.complete()||s.ready!=1||s.selection.reservedBytes>2097152;
                std::printf("world frame=%d ready=%zu staging=%zu reserved=%zu error=%s\n",frame,s.ready,s.stagingBytes,s.selection.reservedBytes,s.error.c_str());}
            if(empty)failed|=scene.status().ready!=0;
            auto origin=world::SpatialWorld(128).cell(position);auto local=world::relativeTo(position,origin,128);
            render::RTSCamera camera;camera.target={float(local.x.toDouble()),4,float(local.z.toDouble())+32};camera.distance=48;camera.pitch=.25f;camera.yaw=3.14159265f;
            renderer.beginScene(camera,d.framebufferWidth(),d.framebufferHeight());scene.draw(renderer,origin,128);
            gfx::PassDesc pass;pass.clearColor[0]=.08f;pass.clearColor[1]=.12f;pass.clearColor[2]=.18f;renderer.render(pass);++frame;
        };
        callbacks.shutdown=[&]{scene.shutdown(*device);failed|=scene.status().ready!=0;renderer.shutdown();};
        auto result=platform::run(config,callbacks);return result?result:(failed?1:0);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 2;}
}
